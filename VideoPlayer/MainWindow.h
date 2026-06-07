#pragma once

#include <QMainWindow>
#include <QList>
#include <optional>
#include "DatabaseManager.h"
#include "PlaylistModel.h"

// Forward declarations — avoids including heavy Qt headers in every TU
class MpvPlayerWidget;
class QListView;
class QLineEdit;
class QPushButton;
class QLabel;
class QSpinBox;
class QTextEdit;
class QSlider;
class QComboBox;
class QSplitter;
class QModelIndex;
class QAction;
class ThumbnailLabel;   // defined file-scope in MainWindow.cpp

// ============================================================
// MainWindow
//   Top-level window that wires together:
//     - DatabaseManager  (persistence)
//     - PlaylistModel    (Qt model / filtering)
//     - MpvPlayerWidget  (video playback)
//   Layout:
//     Left  panel — search bar, playlist, index-sheet thumbnail,
//                   rating (0-100) + memo, save button
//     Right panel — video surface, seek/volume/upscale controls
// ============================================================
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

    // Open and immediately play a file passed from outside the app —
    // typically argv[1] when Windows launches us via "Open with" or a
    // default-app file association. Registers the file in the playlist
    // (idempotent) and starts playback. Safe to call right after show().
    void openExternalFile(const QString& path);

    // ---- Repeat mode ----
    enum class RepeatMode { None, One, All };

private slots:
    // ---- Playlist management ----
    void onAddFiles();
    void onAddFolder();
    void onRemoveSelected();
    void onSearchTextChanged(const QString& text);
    void onPlaylistItemSelected(const QModelIndex& current, const QModelIndex& previous);
    void onPlaylistItemDoubleClicked(const QModelIndex& index);

    // ---- Metadata ----
    void onSaveMetadata();

    // ---- Playback navigation ----
    void onPlayPrevious();
    void onPlayNext();
    void onPlayPauseRequested();   // Space / play-pause button

    // ---- Repeat ----
    void onRepeatClicked();

    // ---- Playback speed ----
    void onSpeedChanged(int comboIndex);   // speed combo selection
    void onSpeedUp();                       // shortcut: next faster step
    void onSpeedDown();                     // shortcut: next slower step

    // ---- A-B repeat ----
    void onSetPointA();   // mark loop start at current position
    void onSetPointB();   // mark loop end   at current position
    void onClearAB();     // clear both points

    // ---- Frame stepping ----
    void onFrameStep();       // one frame forward (pauses)
    void onFrameBackStep();   // one frame backward (pauses)

    // ---- MPV event handlers ----
    void onPositionChanged(double seconds);
    void onDurationChanged(double seconds);
    void onPauseStateChanged(bool paused);
    void onFileEnded();

    // ---- Seek slider ----
    void onSeekSliderPressed();
    void onSeekSliderReleased();

    // ---- Relative seek (keyboard shortcut) ----
    void onSeekBack();
    void onSeekForward();

    // ---- Thumbnail ----
    void onTakeScreenshot();
    void onImportThumbnail();
    void onAutoThumbnail();

    // ---- Tools / Options ----
    void onOptionsTriggered();

    // ---- View ----
    void onToggleFullscreen();
    void onExitFullscreen();    // Esc — no-op when not fullscreen

protected:
    void resizeEvent(QResizeEvent* e) override;
    // Persist the currently-playing item's resume position on shutdown
    // ("이어보기") before the window (and mpv) tears down.
    void closeEvent(QCloseEvent* e) override;

private:
    // ---- Core components (owned by this window) ----
    DatabaseManager  m_db;
    PlaylistModel*   m_playlistModel{nullptr};
    MpvPlayerWidget* m_mpvWidget{nullptr};

    // ---- Top-level layout blocks (for fullscreen hide/show) ----
    QWidget*         m_leftPanel{nullptr};
    QWidget*         m_rightPanel{nullptr};
    QWidget*         m_controlsBar{nullptr};
    QByteArray       m_savedGeometry;   // geometry snapshot before entering fullscreen
    QTimer*          m_fsHideTimer{nullptr};  // auto-hide controls overlay in fullscreen

    // ---- Left panel widgets ----
    QLineEdit*       m_searchEdit{nullptr};
    QSpinBox*        m_minRatingSpinBox{nullptr};
    QListView*       m_playlistView{nullptr};
    ThumbnailLabel*  m_thumbnailLabel{nullptr};
    QSpinBox*        m_ratingSpinBox{nullptr};
    QTextEdit*   m_memoEdit{nullptr};
    QPushButton* m_saveButton{nullptr};
    QPushButton* m_importThumbButton{nullptr};
    QPushButton* m_autoThumbButton{nullptr};
    // Search-row icon buttons — kept as members so refreshIcons() can
    // recolour them when the theme changes live.
    QPushButton* m_addButton{nullptr};
    QPushButton* m_addFolderButton{nullptr};
    QPushButton* m_removeButton{nullptr};

    // ---- Right panel / controls bar widgets ----
    QPushButton* m_prevButton{nullptr};
    QPushButton* m_playPauseButton{nullptr};
    QPushButton* m_stopButton{nullptr};
    QPushButton* m_nextButton{nullptr};
    QSlider*     m_seekSlider{nullptr};
    QLabel*      m_timeLabel{nullptr};
    QPushButton* m_volumeButton{nullptr};
    QSlider*     m_volumeSlider{nullptr};
    int          m_lastVolume{80};   // restored when un-muting
    QComboBox*   m_upscaleCombo{nullptr};
    QPushButton* m_screenshotButton{nullptr};
    QPushButton* m_repeatButton{nullptr};
    QComboBox*   m_speedCombo{nullptr};
    QPushButton* m_abAButton{nullptr};
    QPushButton* m_abBButton{nullptr};
    QPushButton* m_abClearButton{nullptr};
    QPushButton* m_frameBackButton{nullptr};
    QPushButton* m_frameFwdButton{nullptr};

    // ---- QActions (own their shortcuts — registered in createActions) ----
    // Every action customisable from the Options dialog must be kept in
    // m_shortcutActions so the dialog can iterate them generically.
    QAction* m_actPlayPause{nullptr};
    QAction* m_actStop{nullptr};
    QAction* m_actSeekBack{nullptr};
    QAction* m_actSeekForward{nullptr};
    QAction* m_actPrevious{nullptr};
    QAction* m_actNext{nullptr};
    QAction* m_actAddFiles{nullptr};
    QAction* m_actAddFolder{nullptr};
    QAction* m_actRemoveSelected{nullptr};
    QAction* m_actSaveMeta{nullptr};
    QAction* m_actScreenshot{nullptr};
    QAction* m_actToggleRepeat{nullptr};
    QAction* m_actSpeedUp{nullptr};
    QAction* m_actSpeedDown{nullptr};
    QAction* m_actSetPointA{nullptr};
    QAction* m_actSetPointB{nullptr};
    QAction* m_actClearAB{nullptr};
    QAction* m_actFrameStep{nullptr};
    QAction* m_actFrameBackStep{nullptr};
    QAction* m_actToggleFullscreen{nullptr};
    QAction* m_actExitFullscreen{nullptr};
    QAction* m_actOptions{nullptr};
    QAction* m_actQuit{nullptr};

    QList<QAction*> m_shortcutActions;   // subset surfaced in Options dialog

    // ---- Transient state ----
    // m_currentItem = the entry SELECTED in the playlist (drives the
    //   rating/memo/thumbnail panel + metadata edits). Overwritten every
    //   time the selection changes — even mid-playback.
    // m_playingItem = the entry actually PLAYING. Set only when playback
    //   starts; this is what next/previous/auto-advance key off so that
    //   merely selecting another row does not hijack the play queue.
    std::optional<MediaItem> m_currentItem;
    std::optional<MediaItem> m_playingItem;
    bool                     m_userSeeking{false};
    double                   m_duration{0.0};
    RepeatMode               m_repeatMode{RepeatMode::None};

    // ---- "이어보기" (resume playback) ----
    // m_resumeEnabled — user option (Options dialog). When off, we neither
    //   restore nor (effectively) act on saved positions.
    // m_pendingResumePos — position to seek to once the file is actually
    //   loaded. Set in playItemAtRow when a meaningful resume point exists;
    //   consumed (and cleared) by the fileLoaded handler. Seeking right
    //   after loadFile() is too early — mpv hasn't opened the file yet.
    bool                     m_resumeEnabled{true};
    double                   m_pendingResumePos{0.0};

    // ---- A-B repeat (transient — never persisted) ----
    // When both are set and B > A, onPositionChanged loops playback back
    // to A whenever the position runs past B (or jumps before A).
    std::optional<double>    m_pointA;
    std::optional<double>    m_pointB;

    // ---- Setup helpers ----
    void     setupDatabase();
    void     setupUI();
    void     setupConnections();
    void     createActions();
    void     setupMenuBar();
    void     loadShortcuts();
    void     saveShortcuts();
    QWidget* buildLeftPanel();

    // ---- Playlist view mode (list vs. thumbnail grid) ----
    // m_playlistGridMode mirrors the persisted "ui/playlistView" choice;
    // applyPlaylistViewMode reconfigures m_playlistView in place so the
    // toggle applies live without a restart and without disturbing
    // selection / double-click / drag-drop wiring.
    bool m_playlistGridMode{false};
    void applyPlaylistViewMode(bool grid);

    QWidget* buildRightPanel();
    QWidget* buildControlsBar();
    void     updateRepeatButton();
    void     updateABButtons();   // recolour A/B/clear to reflect armed state

    // Re-set the QIcon on every icon button from the current ThemeManager
    // colours. Called after a live theme/accent change so glyphs recolour
    // without a restart. Also re-runs the dynamic-icon updaters so the
    // repeat/A-B/play-pause icons reflect both state AND the new theme.
    void     refreshIcons();

    // ---- Runtime helpers ----
    void loadCurrentItem(const MediaItem& item);
    void updateThumbnailDisplay(const QString& path);
    void refreshPlaylist();
    void playItemAtRow(int row);

    // "이어보기": persist the OUTGOING playing item's position before we
    // switch away from it (or shut down). Saves only when the position is
    // sane (> 0 and strictly before the end) to avoid clobbering a good
    // resume point with a 0 or an at-the-very-end value.
    void saveCurrentResumePos();

    [[nodiscard]] int     currentPlaylistRow() const;
    // Row of the item actually playing (m_currentItem) in the current
    // filtered view; falls back to the view's selection when nothing is
    // playing. Navigation/auto-advance must key off this, NOT the user's
    // manual selection — otherwise selecting another row mid-playback
    // makes the next track advance from the wrong place.
    [[nodiscard]] int     currentPlayingRow() const;
    [[nodiscard]] QString formatTime(double seconds) const;
    void positionFsControlsBar();

    // Qt event filter for double-click on thumbnail label
    bool eventFilter(QObject* obj, QEvent* event) override;
};
