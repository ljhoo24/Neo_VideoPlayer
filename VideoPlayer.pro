################################################################################
# VideoPlayer.pro  —  Qt qmake build file (alternative to CMakeLists.txt)
#
# Usage:
#   qmake VideoPlayer.pro MPV_ROOT=C:/tools/mpv-dev-x86_64
#   nmake (Windows) or make (Linux/macOS)
################################################################################

QT       += core gui widgets sql
CONFIG   += c++20 warn_on
TARGET    = VideoPlayer
TEMPLATE  = app

# Remove console window on Windows release builds
win32: CONFIG += windows

# Application icon (Windows)
win32: RC_ICONS = icon.ico

# ── Source files ──────────────────────────────────────────────────────────────
HEADERS += \
    VideoPlayer/MainWindow.h          \
    VideoPlayer/DatabaseManager.h     \
    VideoPlayer/MpvPlayerWidget.h     \
    VideoPlayer/PlaylistModel.h

SOURCES += \
    VideoPlayer/main.cpp              \
    VideoPlayer/MainWindow.cpp        \
    VideoPlayer/DatabaseManager.cpp   \
    VideoPlayer/MpvPlayerWidget.cpp   \
    VideoPlayer/PlaylistModel.cpp

# ── libmpv ────────────────────────────────────────────────────────────────────
# Set MPV_ROOT on the qmake command line, e.g.:
#   qmake MPV_ROOT=C:/tools/mpv-dev-x86_64
isEmpty(MPV_ROOT) {
    win32:   MPV_ROOT = C:/tools/mpv-dev-x86_64
    unix:    MPV_ROOT = /usr/local
}

INCLUDEPATH += $$MPV_ROOT/include
LIBS        += -L$$MPV_ROOT/lib -lmpv

# ── Windows: extra post-build step to copy the mpv DLL ───────────────────────
win32 {
    MPV_DLL = $$MPV_ROOT/bin/mpv-2.dll
    QMAKE_POST_LINK += \
        $$QMAKE_COPY $$shell_path($$MPV_DLL) \
                     $$shell_path($$OUT_PWD/release/) $$escape_expand(\\n\\t)
}

# ── Deployment ────────────────────────────────────────────────────────────────
# Run windeployqt after the build (Windows only)
win32:release {
    QMAKE_POST_LINK += \
        windeployqt --no-translations $$shell_path($$OUT_PWD/release/VideoPlayer.exe)
}
