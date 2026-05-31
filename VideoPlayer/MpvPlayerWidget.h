#pragma once

#include <QWidget>

// Forward-declare the opaque mpv C types to avoid polluting headers
struct mpv_handle;
struct mpv_event;

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
    bool        m_initialized{false};

    void observeProperties();
    void applyUpscalingProfile();
    void handleMpvEvent(mpv_event* event);

    // Static C-style callback required by the mpv C API
    static void wakeupCallback(void* ctx);
};
