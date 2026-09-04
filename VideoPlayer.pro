################################################################################
# VideoPlayer.pro  —  Qt qmake build file (alternative to CMakeLists.txt)
#
# Usage:
#   qmake VideoPlayer.pro MPV_ROOT=C:/tools/mpv-dev-x86_64
#   nmake (Windows) or make (Linux/macOS)
################################################################################

QT       += core gui widgets sql network
CONFIG   += c++20 warn_on
TARGET    = VideoPlayer
TEMPLATE  = app

lessThan(QT_MAJOR_VERSION, 6): error(VideoPlayer requires Qt 6.5 or newer)
equals(QT_MAJOR_VERSION, 6):lessThan(QT_MINOR_VERSION, 5): \
    error(VideoPlayer requires Qt 6.5 or newer)

# Remove console window on Windows release builds
win32: CONFIG += windows

# Application icon (Windows)
win32: RC_ICONS = icon.ico

# ── Source files ──────────────────────────────────────────────────────────────
HEADERS += \
    VideoPlayer/MainWindow.h          \
    VideoPlayer/DatabaseManager.h     \
    VideoPlayer/MpvPlayerWidget.h     \
    VideoPlayer/PlaylistModel.h       \
    VideoPlayer/OptionsDialog.h       \
    VideoPlayer/IconFont.h            \
    VideoPlayer/ThemeManager.h

SOURCES += \
    VideoPlayer/main.cpp              \
    VideoPlayer/MainWindow.cpp        \
    VideoPlayer/DatabaseManager.cpp   \
    VideoPlayer/MpvPlayerWidget.cpp   \
    VideoPlayer/PlaylistModel.cpp     \
    VideoPlayer/OptionsDialog.cpp     \
    VideoPlayer/IconFont.cpp          \
    VideoPlayer/ThemeManager.cpp

RESOURCES += VideoPlayer/resources/resources.qrc

# ── libmpv ────────────────────────────────────────────────────────────────────
# Set MPV_ROOT on the qmake command line, e.g.:
#   qmake MPV_ROOT=C:/tools/mpv-dev-x86_64
isEmpty(MPV_ROOT) {
    win32:   MPV_ROOT = C:/tools/mpv-dev-x86_64
    unix:    MPV_ROOT = /usr/local
}

INCLUDEPATH += $$MPV_ROOT/include
LIBS        += -L$$MPV_ROOT -L$$MPV_ROOT/lib -lmpv

# ── Windows: extra post-build step to copy the mpv DLL ───────────────────────
win32 {
    MPV_DLL = $$MPV_ROOT/libmpv-2.dll
    !exists($$MPV_DLL): MPV_DLL = $$MPV_ROOT/bin/libmpv-2.dll
    !exists($$MPV_DLL): MPV_DLL = $$MPV_ROOT/bin/mpv-2.dll
    QMAKE_POST_LINK += \
        $$QMAKE_COPY $$shell_path($$MPV_DLL) \
                     $$shell_path($$OUT_PWD/release/) $$escape_expand(\\n\\t)
}

# ── Deployment ────────────────────────────────────────────────────────────────
# Run windeployqt after the build (Windows only)
win32:release {
    WINDEPLOYQT = $$shell_path($$[QT_INSTALL_BINS]/windeployqt.exe)
    SHADER_DIR  = $$shell_path($$OUT_PWD/release/shaders)
    SHADER_SRC  = $$shell_path($$PWD/VideoPlayer/shaders/NVScaler.glsl)
    QMAKE_POST_LINK += \
        $$WINDEPLOYQT --no-translations $$shell_path($$OUT_PWD/release/VideoPlayer.exe) \
        $$escape_expand(\n\t) \
        $$QMAKE_MKDIR $$SHADER_DIR \
        $$escape_expand(\n\t) \
        $$QMAKE_COPY $$SHADER_SRC $$SHADER_DIR
}
