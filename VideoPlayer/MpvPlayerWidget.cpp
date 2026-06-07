#include "MpvPlayerWidget.h"

#include <QMetaObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QStringList>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QDebug>

#include <algorithm>   // std::clamp
#include <cstdlib>     // std::atoi
#include <stdexcept>
#include <string_view>

// libmpv is a C library — wrap in extern "C" to prevent name mangling
extern "C"
{
#include <mpv/client.h>
}

// ============================================================
// Constructor / Destructor
// ============================================================

MpvPlayerWidget::MpvPlayerWidget(QWidget* parent)
    : QWidget(parent)
{
    // Force creation of a native (HWND) window handle so mpv can
    // embed its renderer into this widget.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_DontCreateNativeAncestors);

    // Don't steal keyboard focus — MainWindow's QAction shortcuts
    // (space, arrows, etc.) must reach the application even when the
    // user clicks on the video surface.
    setFocusPolicy(Qt::NoFocus);

    m_mpv = mpv_create();
    if (!m_mpv)
        throw std::runtime_error("MpvPlayerWidget: mpv_create() failed");

    // ---- Configure before initialize ----
    // Use the GPU video output with automatic hardware decoding
    mpv_set_option_string(m_mpv, "vo",          "gpu");
    mpv_set_option_string(m_mpv, "hwdec",       "auto-safe");
    // keep-open must stay "no" (the default). With "yes" mpv suppresses
    // MPV_END_FILE_REASON_EOF at natural end-of-file and pauses on the
    // last frame instead — which silently breaks both repeat-one and
    // auto-advance to the next playlist entry, since onFileEnded never
    // fires. If a "hold last frame" UX is wanted later, observe the
    // "eof-reached" property instead of re-enabling keep-open.
    mpv_set_option_string(m_mpv, "keep-open",   "no");
    mpv_set_option_string(m_mpv, "volume",      "80");
    mpv_set_option_string(m_mpv, "scale",       "bilinear");  // default: fast path
    mpv_set_option_string(m_mpv, "cscale",      "bilinear");

    // Embed the mpv renderer into this Qt native window.
    // mpv_set_option requires a non-const void* pointer, so use a local variable.
    int64_t wid = static_cast<int64_t>(winId());
    mpv_set_option(m_mpv, "wid", MPV_FORMAT_INT64, &wid);

    if (mpv_initialize(m_mpv) < 0)
    {
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        throw std::runtime_error("MpvPlayerWidget: mpv_initialize() failed");
    }

    m_initialized = true;

    // Forward mpv log output into Qt's logging so we can capture it in
    // app.log. "info" covers info/warn/error/fatal — enough to diagnose
    // codec failures ("Could not open codec", "No decoder for …") while
    // staying below the verbose spam of "v"/"debug".
    if (mpv_request_log_messages(m_mpv, "info") < 0)
        qWarning() << "[mpv] mpv_request_log_messages failed";

    // Register the wakeup callback — mpv calls this from its internal
    // thread whenever new events are ready. We bounce to the Qt main
    // thread via QMetaObject::invokeMethod + QueuedConnection.
    mpv_set_wakeup_callback(m_mpv, &MpvPlayerWidget::wakeupCallback, this);

    // Watch properties we need for the UI
    observeProperties();

}

MpvPlayerWidget::~MpvPlayerWidget()
{
    if (m_mpv)
    {
        // Detach the wakeup callback before destroy to avoid a race
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_destroy(m_mpv);
    }
}

// ============================================================
// Public playback control
// ============================================================

void MpvPlayerWidget::loadFile(const QString& filePath)
{
    if (!m_mpv)
        return;

    // Windows UNC path fix.
    //
    // Qt exposes UNC paths as "//server/share/..." (forward slashes).
    // libmpv's Windows file backend mishandles that form — it collapses
    // the leading "//" into a single "\", turning a valid UNC path into
    // a broken "\server\share\..." and failing with "Invalid argument".
    //
    // QDir::toNativeSeparators replaces every '/' with '\' on Windows,
    // preserving the doubled separator, which yields the canonical UNC
    // form "\\server\share\..." that libmpv opens correctly.
    // Drive-letter paths ("C:/foo") work either way, so applying this
    // universally is safe.
    const QString   nativePath = QDir::toNativeSeparators(filePath);
    const QByteArray utf8      = nativePath.toUtf8();

    qDebug() << "[mpv] loadfile:" << nativePath;

    const char* args[] = { "loadfile", utf8.constData(), nullptr };
    mpv_command(m_mpv, args);

    // mpv's "pause" is a global runtime state, not per-file — a new
    // loadfile inherits whatever pause state was active for the previous
    // clip. So if the user paused or stopped before picking another
    // entry from the playlist, the new file loads but never starts.
    // Force pause=no on every load so double-click / next / repeat all
    // begin playback immediately. The property write goes through after
    // the loadfile command is queued; mpv applies it to the file that
    // becomes current.
    int unpause = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &unpause);
}

void MpvPlayerWidget::stop()
{
    if (!m_mpv)
        return;

    const char* args[] = { "stop", nullptr };
    mpv_command(m_mpv, args);
}

bool MpvPlayerWidget::hasFile() const
{
    if (!m_mpv)
        return false;
    // `idle-active` is the canonical "no file currently loaded" flag.
    // It's true between application start and the first loadfile, and
    // also after a `stop` command. We invert it so the API reads
    // naturally at the call site: hasFile() == "togglePause is
    // meaningful right now".
    int idleFlag = 0;
    if (mpv_get_property(m_mpv, "idle-active", MPV_FORMAT_FLAG, &idleFlag) < 0)
        return false;
    return idleFlag == 0;
}

void MpvPlayerWidget::togglePause()
{
    if (!m_mpv)
        return;

    const char* args[] = { "cycle", "pause", nullptr };
    mpv_command(m_mpv, args);
}

// ============================================================
// Mouse gestures over the video surface
//
// With "wid" embedding mpv's child HWND eats the raw mouse messages and
// never forwards them to the Qt parent, so QWidget event overrides are
// useless here. Instead we bind the gestures inside mpv's own input
// system to `script-message` commands. mpv fires those back to every
// client as MPV_EVENT_CLIENT_MESSAGE; handleMpvEvent() catches them and
// emits the high-level signals below. MainWindow owns the actual volume
// / fullscreen state, so it keeps the slider + window mode consistent.
//
// Binding the wheel to a script-message (rather than mpv's built-in
// "add volume") deliberately overrides mpv's default wheel→volume/seek
// bindings, keeping volume single-sourced through MainWindow's slider.
// ============================================================

void MpvPlayerWidget::wheelEvent(QWheelEvent* e)
{
    // mpv's render HWND is WS_DISABLED, so Windows forwards mouse input
    // over the video to this parent widget. One notch = 120 units.
    const int notches = e->angleDelta().y() / 120;
    if (notches != 0)
        emit wheelVolumeStep(notches);
    e->accept();
}

void MpvPlayerWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        emit doubleClicked();
    e->accept();
}

void MpvPlayerWidget::seek(double seconds)
{
    if (!m_mpv)
        return;

    mpv_set_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &seconds);
}

void MpvPlayerWidget::seekRelative(double seconds)
{
    if (!m_mpv)
        return;

    // mpv's "seek <value> relative" command — handles clamping at 0/eof.
    // QByteArray owns the formatted string for the duration of the call.
    const QByteArray secStr = QByteArray::number(seconds, 'f', 3);
    const char* args[] = { "seek", secStr.constData(), "relative", nullptr };
    mpv_command(m_mpv, args);
}

void MpvPlayerWidget::setVolume(int volume)
{
    if (!m_mpv)
        return;

    double vol = static_cast<double>(std::clamp(volume, 0, 100));
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
}

// ============================================================
// Playback speed / frame stepping
// ============================================================

void MpvPlayerWidget::setSpeed(double speed)
{
    // Clamp to mpv's sane audio-pitch-correctable range. We cache the
    // value so speed() can report it without a property round-trip —
    // mpv keeps "speed" global across loadfile, so the cache stays valid.
    m_speed = std::clamp(speed, 0.25, 4.0);
    if (!m_mpv)
        return;
    mpv_set_property(m_mpv, "speed", MPV_FORMAT_DOUBLE, &m_speed);
}

void MpvPlayerWidget::frameStep()
{
    if (!m_mpv)
        return;
    // Advances exactly one frame and pauses (mpv built-in semantics).
    const char* args[] = { "frame-step", nullptr };
    mpv_command(m_mpv, args);
}

void MpvPlayerWidget::frameBackStep()
{
    if (!m_mpv)
        return;
    // Steps one frame backward and pauses. Slower than forward stepping
    // (mpv re-decodes from the preceding keyframe) but accurate.
    const char* args[] = { "frame-back-step", nullptr };
    mpv_command(m_mpv, args);
}

// ============================================================
// Feature: video adjustments
//
// Aspect override, rotation, zoom/pan and deinterlace are plain mpv
// runtime properties — orthogonal to the upscale vf chain, so setting
// them never disturbs applyUpscalingProfile (and vice versa). The
// accumulating helpers (zoomIn/Out, rotateStep) keep their running value
// in members so the menu/shortcuts can nudge from the current state.
// ============================================================

void MpvPlayerWidget::setAspectOverride(const QString& ratio)
{
    if (!m_mpv)
        return;
    // mpv wants the literal "-1" for "auto / use container aspect".
    const QByteArray value =
        ratio.isEmpty() ? QByteArray("-1") : ratio.toUtf8();
    mpv_set_property_string(m_mpv, "video-aspect-override", value.constData());
}

void MpvPlayerWidget::setRotate(int degrees)
{
    // Normalise into 0/90/180/270 so the running state stays canonical
    // even if a caller hands us 360 or a negative.
    m_rotate = ((degrees % 360) + 360) % 360;
    if (!m_mpv)
        return;
    int64_t rot = m_rotate;
    mpv_set_property(m_mpv, "video-rotate", MPV_FORMAT_INT64, &rot);
}

void MpvPlayerWidget::rotateStep()
{
    setRotate(m_rotate + 90);   // wraps 270 -> 0 via setRotate's normalise
}

void MpvPlayerWidget::setZoom(double zoom)
{
    // mpv's video-zoom is a log2 factor. Clamp to a sane band so the user
    // can't zoom into a single pixel or shrink the frame to nothing.
    m_zoom = std::clamp(zoom, -2.0, 3.0);
    if (!m_mpv)
        return;
    mpv_set_property(m_mpv, "video-zoom", MPV_FORMAT_DOUBLE, &m_zoom);
}

void MpvPlayerWidget::zoomIn()
{
    setZoom(m_zoom + 0.25);   // ~+19% per step on the log2 scale
}

void MpvPlayerWidget::zoomOut()
{
    setZoom(m_zoom - 0.25);
}

void MpvPlayerWidget::setPan(double x, double y)
{
    m_panX = std::clamp(x, -1.0, 1.0);
    m_panY = std::clamp(y, -1.0, 1.0);
    if (!m_mpv)
        return;
    mpv_set_property(m_mpv, "video-pan-x", MPV_FORMAT_DOUBLE, &m_panX);
    mpv_set_property(m_mpv, "video-pan-y", MPV_FORMAT_DOUBLE, &m_panY);
}

void MpvPlayerWidget::setDeinterlace(bool on)
{
    m_deinterlace = on;
    if (!m_mpv)
        return;
    mpv_set_property_string(m_mpv, "deinterlace", on ? "yes" : "no");
}

void MpvPlayerWidget::resetVideoAdjustments()
{
    setAspectOverride(QString{});   // auto (-1)
    setRotate(0);
    setZoom(0.0);
    setPan(0.0, 0.0);
    setDeinterlace(false);
}

// ============================================================
// Feature: audio processing (normalize + bass/treble/preamp boost)
//
// Everything is funnelled through mpv's "af" property — a single
// ffmpeg-style audio-filter chain string. We compose that string from
// the enabled pieces in rebuildAudioChain() and set it whole, so each
// change replaces the chain rather than stacking filters.
//
// Filter syntax: ffmpeg filters are wrapped in mpv's `lavfi=[...]`
// form, which is the well-supported way to reach libavfilter from
// mpv's af chain. Multiple filters are joined with ',' into one value.
// Only non-default (non-zero / enabled) pieces are included; an empty
// result clears "af" entirely.
//
// "af" is GLOBAL in mpv (persists across loadfile), so applying on
// change is enough — no per-file re-apply required.
// ============================================================

void MpvPlayerWidget::setAudioNormalize(bool on)
{
    m_audioNormalize = on;
    rebuildAudioChain();
}

void MpvPlayerWidget::setBassGain(int dB)
{
    m_bassGain = std::clamp(dB, -12, 12);
    rebuildAudioChain();
}

void MpvPlayerWidget::setTrebleGain(int dB)
{
    m_trebleGain = std::clamp(dB, -12, 12);
    rebuildAudioChain();
}

void MpvPlayerWidget::setPreampGain(int dB)
{
    m_preampGain = std::clamp(dB, -12, 12);
    rebuildAudioChain();
}

void MpvPlayerWidget::resetAudio()
{
    m_audioNormalize = false;
    m_bassGain       = 0;
    m_trebleGain     = 0;
    m_preampGain     = 0;
    rebuildAudioChain();
}

void MpvPlayerWidget::rebuildAudioChain()
{
    if (!m_mpv)
        return;

    // Collect each enabled piece as a `lavfi=[...]` segment, then join
    // them with ',' into one af value. Order: normalize first so the
    // tone/boost shaping acts on the leveled signal; preamp last so it
    // sets the final output gain.
    QStringList pieces;

    if (m_audioNormalize)
        pieces << "lavfi=[dynaudnorm]";          // realtime loudness normalize
    if (m_bassGain != 0)
        pieces << QString("lavfi=[bass=g=%1]").arg(m_bassGain);
    if (m_trebleGain != 0)
        pieces << QString("lavfi=[treble=g=%1]").arg(m_trebleGain);
    if (m_preampGain != 0)
        pieces << QString("lavfi=[volume=%1dB]").arg(m_preampGain);

    const QString   chain = pieces.join(',');   // empty → clears "af"
    const QByteArray utf8 = chain.toUtf8();

    const int rc = mpv_set_property_string(m_mpv, "af", utf8.constData());
    if (rc < 0)
    {
        qWarning().nospace()
            << "[mpv] set af=\"" << utf8.constData()
            << "\" failed: " << mpv_error_string(rc);
    }
    else
    {
        qDebug().nospace() << "[mpv] af = \"" << utf8.constData() << "\"";
    }
}

// ============================================================
// Feature: three-step rendering profile (UpscaleMode)
//
//  Off       — bilinear everywhere, no post-processing (fast)
//  Standard  — ewa_lanczossharp + sigmoid + deband + light ffmpeg
//              `unsharp` pass. Works on any GPU. Visibly sharper than
//              Off, but conservative on the unsharp amount so NIS has
//              clear room to look stronger.
//  NvidiaNis — NVIDIA Image Scaling: a GLSL compute shader that does
//              its own scaling + content-adaptive sharpening. When
//              this mode is requested we disable mpv's own
//              `scale=ewa_lanczossharp` (NIS supersedes it) and drop
//              the unsharp vf (NIS already sharpens). Sigmoid and
//              deband stay — they're orthogonal post-processing.
//              If the shader file isn't present, we log a warning and
//              fall back to Standard so the user still gets *some*
//              quality bump instead of silently rendering Off.
// ============================================================

namespace
{
    // Returns the absolute path to the bundled NIS shader template (the
    // pristine file dropped by the build system into shaders/ next to
    // the exe). Empty string if missing.
    QString nisShaderSourcePath()
    {
        const QString p = QCoreApplication::applicationDirPath()
                        + "/shaders/NVScaler.glsl";
        return QFileInfo::exists(p) ? p : QString{};
    }

    // Writable per-user location where we drop the patched copy of the
    // shader. We bake the sharpness value (×100, integer) into the
    // filename so changing the slider produces a *different* path on
    // disk. That matters because mpv compares the incoming
    // glsl-shaders string against the current one and skips reload on
    // a string match — keeping a single fixed runtime filename meant
    // sharpness changes never took effect, and worse, the previous
    // empty-string-bounce workaround toggled the vo init state twice
    // while mpv was idle, which left the renderer broken enough to
    // block the first loadfile entirely. A per-value filename
    // sidesteps both problems with one well-defined property write.
    QString nisRuntimeShaderPath(double sharpness)
    {
        const QString dir = QStandardPaths::writableLocation(
                                QStandardPaths::AppDataLocation)
                          + "/shaders";
        QDir().mkpath(dir);

        const int bucket = static_cast<int>(
            std::clamp(sharpness, 0.0, 1.0) * 100.0 + 0.5);
        return QString("%1/NVScaler-runtime-%2.glsl")
                 .arg(dir)
                 .arg(bucket, 3, 10, QChar('0'));   // zero-pad to 3 digits
    }

    // Reads the template shader, rewrites the SHARPNESS define to the
    // requested value, writes it to the per-user runtime location, and
    // returns that path. On any I/O failure returns the original
    // template path so playback keeps working at the default sharpness
    // baked into the bundled file.
    //
    // Why text-patch instead of feeding the value through some mpv-
    // side mechanism: mpv's `glsl-shaders` property only takes file
    // paths, and there is no documented way to inject macro defines
    // into a hooked shader from outside. Rewriting the file is the
    // shortest path that actually changes behaviour.
    QString writeNisShaderWithSharpness(double sharpness)
    {
        const QString src = nisShaderSourcePath();
        if (src.isEmpty())
            return {};

        QFile in(src);
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            qWarning() << "[mpv] NIS: cannot read template" << src
                       << ":" << in.errorString();
            return src;   // fall back to the on-disk default
        }
        QString contents = QString::fromUtf8(in.readAll());
        in.close();

        // Clamp to the shader's documented range and format with a
        // fixed precision so the resulting file is deterministic (no
        // locale-dependent "0,5" vs "0.5").
        const double clamped = std::clamp(sharpness, 0.0, 1.0);

        // The trailing space is deliberate: the original line is
        //   `#define SHARPNESS 0.25 // Amount of sharpening. 0.0 to 1.0.`
        // Our regex stops just before the `//`, so without restoring
        // the separator the comment would butt directly against the
        // number (`0.500// ...`). Most GLSL compilers tolerate that
        // because `//` ends the token, but some strict drivers fail to
        // parse the macro replacement list — and a failed `#define`
        // here cascades into the whole shader getting rejected, which
        // looks to mpv like "no shader present" rather than a clean
        // compile error. Keeping a space preserves the original
        // tokenisation.
        const QString replacement =
            QString("#define SHARPNESS %1 ")
                .arg(QString::number(clamped, 'f', 3));

        // Replaces only the `#define SHARPNESS <number>` line — anchored
        // so we don't accidentally hit unrelated occurrences of the
        // word in comments.
        static const QRegularExpression re(
            R"(^#define\s+SHARPNESS\s+[^\r\n/]+)",
            QRegularExpression::MultilineOption);
        if (!re.match(contents).hasMatch())
        {
            qWarning() << "[mpv] NIS: SHARPNESS define not found in "
                          "template; using bundled file as-is.";
            return src;
        }
        contents.replace(re, replacement);

        const QString dst = nisRuntimeShaderPath(clamped);
        QFile out(dst);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text
                      | QIODevice::Truncate))
        {
            qWarning() << "[mpv] NIS: cannot write runtime shader"
                       << dst << ":" << out.errorString()
                       << "— using template at default sharpness.";
            return src;
        }
        out.write(contents.toUtf8());
        out.close();

        return dst;
    }
}

void MpvPlayerWidget::setUpscaleMode(UpscaleMode mode)
{
    m_upscaleMode = mode;
    applyUpscalingProfile();
}

void MpvPlayerWidget::setNisSharpness(double sharpness)
{
    m_nisSharpness = std::clamp(sharpness, 0.0, 1.0);

    // Only re-apply the profile if NIS is the active mode — for Off /
    // Standard the value is just stored for "next time NIS is picked".
    if (m_upscaleMode == UpscaleMode::NvidiaNis)
        applyUpscalingProfile();
}

void MpvPlayerWidget::applyUpscalingProfile()
{
    if (!m_mpv || !m_initialized)
        return;

    // Local helper — set a string property and log on failure so we
    // can tell the difference between "property applied" and "libmpv
    // silently rejected the value".
    auto setProp = [this](const char* name, const char* value) -> bool
    {
        const int rc = mpv_set_property_string(m_mpv, name, value);
        if (rc < 0)
        {
            qWarning().nospace()
                << "[mpv] set " << name << "=" << value
                << " failed: " << mpv_error_string(rc);
            return false;
        }
        return true;
    };

    // Helper for the `vf` command — replaces the entire video-filter
    // chain so switching modes doesn't stack filters across clicks.
    auto setVf = [this](const char* spec)
    {
        const char* args[] = { "vf", "set", spec, nullptr };
        const int rc = mpv_command(m_mpv, args);
        if (rc < 0)
        {
            qWarning().nospace()
                << "[mpv] vf set \"" << spec << "\" failed: "
                << mpv_error_string(rc);
        }
    };

    // Resolve NIS shader path up-front so we can fall back to Standard
    // *before* writing any properties — keeps the engine state
    // consistent on shader-missing systems.
    UpscaleMode mode = m_upscaleMode;
    if (mode == UpscaleMode::NvidiaNis && nisShaderSourcePath().isEmpty())
    {
        qWarning() << "[mpv] NIS shader not found next to executable "
                      "(expected shaders/NVScaler.glsl) — falling back "
                      "to Standard upscaling.";
        mode = UpscaleMode::Standard;
    }

    switch (mode)
    {
    case UpscaleMode::Off:
    {
        // Fast default path — bilinear, no deband, no sigmoid, no vf,
        // no glsl-shaders.
        setProp("scale",               "bilinear");
        setProp("cscale",              "bilinear");
        setProp("dscale",              "bilinear");
        setProp("scale-antiring",      "0.0");
        setProp("cscale-antiring",     "0.0");
        setProp("sigmoid-upscaling",   "no");
        setProp("linear-downscaling",  "no");
        setProp("correct-downscaling", "no");
        setProp("deband",              "no");
        setProp("glsl-shaders",        "");
        setVf("");

        qDebug() << "[mpv] upscale mode = Off (bilinear, fast path)";
        break;
    }

    case UpscaleMode::Standard:
    {
        // Approximates `--profile=high-quality` from stand-alone mpv
        // with a moderate unsharp pass. Keeps unsharp_amount=0.8 (was
        // 1.2 earlier) so this mode reads as "balanced" against the
        // stronger NIS option — picking NIS should feel like a
        // noticeable step up rather than a sideways move.
        setProp("scale",               "ewa_lanczossharp");
        setProp("cscale",              "ewa_lanczossharp");
        setProp("dscale",              "mitchell");
        setProp("scale-antiring",      "0.7");
        setProp("cscale-antiring",     "0.7");
        setProp("sigmoid-upscaling",   "yes");
        setProp("linear-downscaling",  "yes");
        setProp("correct-downscaling", "yes");
        setProp("deband",              "yes");
        setProp("deband-iterations",   "2");
        setProp("deband-threshold",    "35");
        setProp("deband-range",        "16");
        setProp("deband-grain",        "5");
        setProp("dither-depth",        "auto");
        setProp("glsl-shaders",        "");

        setVf("unsharp=luma_msize_x=5:luma_msize_y=5:luma_amount=0.8:"
              "chroma_msize_x=5:chroma_msize_y=5:chroma_amount=0.4");

        qDebug() << "[mpv] upscale mode = Standard "
                    "(ewa_lanczossharp + sigmoid + deband + unsharp 0.8)";
        break;
    }

    case UpscaleMode::NvidiaNis:
    {
        // NIS does the scaling and the sharpening. We deliberately
        // hand the scaler back to bilinear (mpv's own scaler runs
        // *after* the shader hook, so leaving ewa_lanczossharp would
        // double-process) and clear the unsharp vf for the same reason.
        setProp("scale",               "bilinear");
        setProp("cscale",              "bilinear");
        setProp("dscale",              "mitchell");
        setProp("scale-antiring",      "0.0");
        setProp("cscale-antiring",     "0.0");
        // Sigmoid + deband are colour-space / banding tweaks, orthogonal
        // to spatial upscaling — keep them so NIS still benefits.
        setProp("sigmoid-upscaling",   "yes");
        setProp("linear-downscaling",  "yes");
        setProp("correct-downscaling", "yes");
        setProp("deband",              "yes");
        setProp("deband-iterations",   "2");
        setProp("deband-threshold",    "35");
        setProp("deband-range",        "16");
        setProp("deband-grain",        "5");
        setProp("dither-depth",        "auto");

        // Materialise a per-user copy of the shader with the current
        // SHARPNESS value patched in, then hand mpv that absolute path.
        // The runtime filename includes the sharpness value, so every
        // distinct value resolves to a distinct path on disk and mpv's
        // "skip reload on string match" check naturally triggers a
        // fresh shader load whenever the user moves the slider. A
        // single property write keeps the vo init state stable, which
        // is critical when this path runs while mpv is still idle (no
        // file loaded yet) — the previous double-write through the
        // empty string left the renderer in a state that blocked the
        // first loadfile from ever starting playback.
        const QString  patched   = writeNisShaderWithSharpness(m_nisSharpness);
        const QByteArray shaderP = patched.toUtf8();
        setProp("glsl-shaders", shaderP.constData());
        setVf("");

        qDebug().nospace()
            << "[mpv] upscale mode = NVIDIA NIS (sharpness="
            << m_nisSharpness << ", shader=" << shaderP.constData() << ")";
        break;
    }
    }

    // Read back the actual scaler mpv is using — if this line prints
    // a value different from what we set, something rejected it and
    // the toggle will look like it does nothing.
    char* active = nullptr;
    if (mpv_get_property(m_mpv, "scale", MPV_FORMAT_STRING, &active) == 0
        && active != nullptr)
    {
        qDebug() << "[mpv] active scale =" << active;
        mpv_free(active);
    }
}

// ============================================================
// Screenshot (used for thumbnail generation)
// ============================================================

void MpvPlayerWidget::takeScreenshot(const QString& outputPath)
{
    if (!m_mpv)
        return;

    const QByteArray utf8 = outputPath.toUtf8();
    const char* args[]    = { "screenshot-to-file", utf8.constData(), "video", nullptr };
    mpv_command(m_mpv, args);
}

// ============================================================
// Video info accessors
//
// Queried live from mpv via mpv_get_property — these properties are
// only populated after a file has been loaded and the decoder has
// configured itself.  Returns zero / empty string on any failure so
// callers can fall back gracefully.
// ============================================================

int MpvPlayerWidget::videoWidth() const
{
    if (!m_mpv)
        return 0;
    int64_t w = 0;
    if (mpv_get_property(m_mpv, "width", MPV_FORMAT_INT64, &w) < 0)
        return 0;
    return static_cast<int>(w);
}

int MpvPlayerWidget::videoHeight() const
{
    if (!m_mpv)
        return 0;
    int64_t h = 0;
    if (mpv_get_property(m_mpv, "height", MPV_FORMAT_INT64, &h) < 0)
        return 0;
    return static_cast<int>(h);
}

double MpvPlayerWidget::videoFps() const
{
    if (!m_mpv)
        return 0.0;
    double fps = 0.0;
    // "container-fps" is closer to the FFprobe / OpenCV value the
    // Python reference reads via CAP_PROP_FPS; "estimated-vf-fps" is
    // a runtime estimate that doesn't exist until playback has begun.
    if (mpv_get_property(m_mpv, "container-fps", MPV_FORMAT_DOUBLE, &fps) < 0
        || fps <= 0.0)
    {
        mpv_get_property(m_mpv, "estimated-vf-fps", MPV_FORMAT_DOUBLE, &fps);
    }
    return fps;
}

QString MpvPlayerWidget::videoCodec() const
{
    if (!m_mpv)
        return {};
    char* raw = nullptr;
    // "video-codec-name" gives the short name (e.g. "h264", "hevc"),
    // which matches the spirit of OpenCV's fourcc string used in the
    // Python reference more closely than the long human-readable
    // "video-codec" property.
    if (mpv_get_property(m_mpv, "video-codec-name", MPV_FORMAT_STRING, &raw) < 0
        || raw == nullptr)
    {
        return {};
    }
    const QString s = QString::fromUtf8(raw);
    mpv_free(raw);
    return s;
}

// ============================================================
// Private: property observation
// ============================================================

void MpvPlayerWidget::observeProperties()
{
    // Reply ID 0 — we don't distinguish by reply_userdata here
    mpv_observe_property(m_mpv, 0, "time-pos",  MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration",  MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause",     MPV_FORMAT_FLAG);
}

// ============================================================
// Private: mpv C-style wakeup callback (called on mpv thread)
// ============================================================

void MpvPlayerWidget::wakeupCallback(void* ctx)
{
    // Must be non-blocking. Post to the Qt event loop instead.
    auto* self = static_cast<MpvPlayerWidget*>(ctx);
    QMetaObject::invokeMethod(self, &MpvPlayerWidget::onMpvEvents,
                              Qt::QueuedConnection);
}

// ============================================================
// Private slot: drain the mpv event queue (Qt main thread)
// ============================================================

void MpvPlayerWidget::onMpvEvents()
{
    while (true)
    {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;

        handleMpvEvent(event);
    }
}

void MpvPlayerWidget::handleMpvEvent(mpv_event* event)
{
    switch (event->event_id)
    {
    case MPV_EVENT_PROPERTY_CHANGE:
    {
        auto* prop = static_cast<mpv_event_property*>(event->data);

        if (std::string_view{prop->name} == "time-pos"
            && prop->format == MPV_FORMAT_DOUBLE)
        {
            m_position = *static_cast<double*>(prop->data);
            emit positionChanged(m_position);
        }
        else if (std::string_view{prop->name} == "duration"
                 && prop->format == MPV_FORMAT_DOUBLE)
        {
            m_duration = *static_cast<double*>(prop->data);
            emit durationChanged(m_duration);
        }
        else if (std::string_view{prop->name} == "pause"
                 && prop->format == MPV_FORMAT_FLAG)
        {
            m_paused = (*static_cast<int*>(prop->data)) != 0;
            emit pauseStateChanged(m_paused);
        }
        break;
    }

    case MPV_EVENT_FILE_LOADED:
    {
        char* rawPath = nullptr;
        if (mpv_get_property(m_mpv, "path", MPV_FORMAT_STRING, &rawPath) == 0
            && rawPath != nullptr)
        {
            emit fileLoaded(QString::fromUtf8(rawPath));
            mpv_free(rawPath);
        }
        break;
    }

    case MPV_EVENT_VIDEO_RECONFIG:
    {
        // Diagnostic: report source video resolution vs current widget
        // size so the "HQ upscale" toggle makes sense.  Upscaling only
        // has a visible effect when viewport > source — otherwise both
        // bilinear and lanczos look identical (pure downscale path).
        int64_t srcW = 0, srcH = 0;
        mpv_get_property(m_mpv, "width",  MPV_FORMAT_INT64, &srcW);
        mpv_get_property(m_mpv, "height", MPV_FORMAT_INT64, &srcH);

        const int viewW = width();
        const int viewH = height();

        const char* dir =
            (viewW > srcW || viewH > srcH) ? "UPSCALE (HQ will show)"
          : (viewW < srcW || viewH < srcH) ? "downscale (HQ subtle)"
                                           : "native (no scaling)";

        qDebug().nospace()
            << "[mpv] video " << srcW << "x" << srcH
            << "  viewport " << viewW << "x" << viewH
            << "  -> " << dir;
        break;
    }

    case MPV_EVENT_END_FILE:
    {
        // 자연 종료(EOF)에만 fileEnded를 쏘자.
        // stop / redirect(loadfile 재호출) / error 시에도 END_FILE가 오는데,
        // 그때마다 반복 로직이 돌면 무한 루프가 된다.
        auto* info = static_cast<mpv_event_end_file*>(event->data);
        if (info)
        {
            const char* reasonStr = "unknown";
            switch (info->reason)
            {
            case MPV_END_FILE_REASON_EOF:      reasonStr = "eof";      break;
            case MPV_END_FILE_REASON_STOP:     reasonStr = "stop";     break;
            case MPV_END_FILE_REASON_QUIT:     reasonStr = "quit";     break;
            case MPV_END_FILE_REASON_ERROR:    reasonStr = "error";    break;
            case MPV_END_FILE_REASON_REDIRECT: reasonStr = "redirect"; break;
            default: break;
            }

            // Log every end so silent-fail codec issues are visible.
            if (info->reason == MPV_END_FILE_REASON_ERROR)
            {
                qWarning().nospace()
                    << "[mpv] END_FILE reason=" << reasonStr
                    << " error="
                    << (info->error ? mpv_error_string(info->error) : "unspecified");
            }
            else
            {
                qDebug().nospace()
                    << "[mpv] END_FILE reason=" << reasonStr
                    << " error="
                    << (info->error ? mpv_error_string(info->error) : "ok");
            }

            if (info->reason == MPV_END_FILE_REASON_EOF)
                emit fileEnded();
        }
        break;
    }

    case MPV_EVENT_LOG_MESSAGE:
    {
        auto* msg = static_cast<mpv_event_log_message*>(event->data);
        if (!msg)
            break;

        // Strip the trailing '\n' mpv always appends so app.log looks clean.
        QByteArray text(msg->text);
        if (text.endsWith('\n'))
            text.chop(1);
        if (text.isEmpty())
            break;

        const QByteArray line =
            QByteArray("[mpv:") + msg->prefix + "] " + text;

        const std::string_view level{msg->level};
        if (level == "fatal" || level == "error")
            qCritical() << line.constData();
        else if (level == "warn")
            qWarning() << line.constData();
        else
            qDebug()   << line.constData();
        break;
    }

    default:
        break;
    }
}
