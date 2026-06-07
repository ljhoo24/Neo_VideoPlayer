#pragma once

#include <QWidget>

// Forward-declare the opaque mpv C types to avoid polluting headers
struct mpv_handle;
struct mpv_event;
class QWheelEvent;
class QMouseEvent;

// ============================================================
// MpvPlayerWidget
//   Embeds libmpv into a native Qt widget via the WID option.
//   All mpv event processing is marshalled to the Qt main thread
//   via the wakeup-callback + QMetaObject::invokeMethod pattern.
// ============================================================
class MpvPlayerWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit MpvPlayerWidget(QWidget* parent = nullptr);
    ~MpvPlayerWidget() override;

    // Non-copyable
    MpvPlayerWidget(const MpvPlayerWidget&)            = delete;
    MpvPlayerWidget& operator=(const MpvPlayerWidget&) = delete;

    // ---- Playback control ----
    void loadFile(const QString& filePath);
    void stop();
    void seek(double seconds);
    void seekRelative(double seconds);    // + forward / - backward
    void setVolume(int volume);     // 0-100

    // ---- Playback speed ----
    // 0.5 .. 2.0 typically; 1.0 = normal. Maps directly to mpv's "speed"
    // property. setSpeed is a no-op-safe wrapper; speed() reads back the
    // last value we cached (mpv keeps "speed" globally across files).
    void   setSpeed(double speed);
    [[nodiscard]] double speed() const noexcept { return m_speed; }

    // ---- Frame stepping ----
    // mpv's frame-step / frame-back-step advance exactly one frame and
    // leave playback paused — the canonical way to inspect a clip frame
    // by frame.
    void frameStep();
    void frameBackStep();

    // ---- Feature toggles ----
    //
    // UpscaleMode — three-step rendering profile that the UI cycles
    // through. The integer values are baked into QSettings, so do NOT
    // renumber existing entries (only append new ones at the end).
    //   Off       0 → bilinear everywhere, no post-processing (fast)
    //   Standard  1 → ewa_lanczossharp + sigmoid + deband + light
    //                 ffmpeg `unsharp` pass (works on any GPU)
    //   NvidiaNis 2 → NVIDIA Image Scaling GLSL compute shader loaded
    //                 via mpv's glsl-shaders. NIS does its own scaling
    //                 + adaptive sharpening, so we drop both
    //                 ewa_lanczossharp and the unsharp vf in this mode.
    enum class UpscaleMode : int
    {
        Off       = 0,
        Standard  = 1,
        NvidiaNis = 2,
    };

    void setUpscaleMode(UpscaleMode mode);
    [[nodiscard]] UpscaleMode upscaleMode() const noexcept { return m_upscaleMode; }

    // NIS sharpening strength (0.0 .. 1.0, default 0.5 — the NIS
    // shader's neutral midpoint). The value is baked into a runtime
    // copy of the shader (the `#define SHARPNESS X.XX` line on top) and
    // mpv is asked to reload it. Has no visible effect unless the
    // current mode is NvidiaNis. Setter triggers an immediate re-apply
    // when in NIS mode; in other modes the value is just stored for
    // later.
    void setNisSharpness(double sharpness);
    [[nodiscard]] double nisSharpness() const noexcept { return m_nisSharpness; }

    // ---- Video adjustments ----
    // These map to standalone mpv runtime properties and are entirely
    // independent of the upscale vf chain (applyUpscalingProfile),
    // so the two features never clobber each other.
    //
    //   setAspectOverride — "video-aspect-override". An empty QString means
    //     "auto" (mpv wants the literal "-1"); otherwise pass a ratio
    //     string mpv understands, e.g. "16:9", "4:3", "1.85:1", "2.35:1".
    //   setRotate          — "video-rotate" in degrees. mpv normalises to
    //     0..359; we feed 0/90/180/270. rotateStep() cycles +90.
    //   setZoom            — "video-zoom", a log2 scale (0 = 100%, 1 = 200%,
    //     -1 = 50%). zoomIn/zoomOut nudge by a fixed step.
    //   setPan             — "video-pan-x" / "video-pan-y", each -1..1.
    //   setDeinterlace     — "deinterlace" ("yes"/"no").
    //   resetVideoAdjustments — back to defaults (auto/0/100%/centre/off).
    void setAspectOverride(const QString& ratio);
    void setRotate(int degrees);
    void rotateStep();                 // cycle +90° (wraps 270 -> 0)
    void setZoom(double zoom);
    void zoomIn();                     // +1 step
    void zoomOut();                    // -1 step
    void setPan(double x, double y);
    void setDeinterlace(bool on);
    void resetVideoAdjustments();

    [[nodiscard]] int    rotate()      const noexcept { return m_rotate; }
    [[nodiscard]] double zoom()        const noexcept { return m_zoom; }
    [[nodiscard]] bool   deinterlace() const noexcept { return m_deinterlace; }

    void takeScreenshot(const QString& outputPath);

    // ---- State queries ----
    [[nodiscard]] bool   isPaused()  const noexcept { return m_paused; }
    [[nodiscard]] double duration()  const noexcept { return m_duration; }
    [[nodiscard]] double position()  const noexcept { return m_position; }

    // True iff mpv currently has a file loaded (i.e. is *not* in the
    // idle state). Used to distinguish "togglePause should resume" from
    // "togglePause has nothing to act on, start playback instead" —
    // e.g. right after app startup when the selection was restored but
    // loadFile has not been called yet.
    [[nodiscard]] bool   hasFile()   const;

    // ---- Video info (queried live from mpv; valid only after a file is loaded) ----
    [[nodiscard]] int     videoWidth()  const;
    [[nodiscard]] int     videoHeight() const;
    [[nodiscard]] double  videoFps()    const;
    [[nodiscard]] QString videoCodec()  const;

public slots:
    void togglePause();

signals:
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pauseStateChanged(bool paused);
    void fileLoaded(const QString& filePath);
    void fileEnded();

    // ---- Mouse gestures over the video surface ----
    // mpv embeds its renderer in a WS_DISABLED child window, so Windows
    // routes mouse input to this parent widget; wheelEvent /
    // mouseDoubleClickEvent emit these. MainWindow owns the resulting
    // state (volume slider, fullscreen window mode) so the UI stays in
    // sync — the widget itself stays stateless.
    void wheelVolumeStep(int deltaSteps);   // positive = wheel up
    void doubleClicked();

protected:
    // mpv embeds its renderer in a WS_DISABLED child HWND, so Windows
    // routes mouse input over the video to THIS parent widget. We handle
    // the gestures here and emit signals; MainWindow owns the state.
    void wheelEvent(QWheelEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private slots:
    // Dispatched from wakeupCallback via QMetaObject::invokeMethod
    void onMpvEvents();

private:
    mpv_handle* m_mpv{nullptr};
    bool        m_paused{true};
    double      m_duration{0.0};
    double      m_position{0.0};
    UpscaleMode m_upscaleMode{UpscaleMode::Off};
    double      m_nisSharpness{0.5};   // 0.0..1.0, NIS shader SHARPNESS
    double      m_speed{1.0};           // playback speed (mpv "speed" property)

    // ---- Video adjustment state (accumulated by the +/- and rotate-step
    // helpers; mirrors the corresponding mpv properties) ----
    int         m_rotate{0};            // 0/90/180/270, "video-rotate"
    double      m_zoom{0.0};            // log2 zoom, "video-zoom"
    double      m_panX{0.0};            // "video-pan-x", -1..1
    double      m_panY{0.0};            // "video-pan-y", -1..1
    bool        m_deinterlace{false};   // "deinterlace"

    bool        m_initialized{false};

    void observeProperties();
    void applyUpscalingProfile();
    void registerMouseBindings();
    void handleMpvEvent(mpv_event* event);

    // Static C-style callback required by the mpv C API
    static void wakeupCallback(void* ctx);
};
