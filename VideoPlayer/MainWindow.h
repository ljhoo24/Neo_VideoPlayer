#pragma once

#include <QMainWindow>
#include <QList>
#include <QPixmap>   // m_seekPreviewSheet is held by value
#include <QString>
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
class QListWidget;
class QListWidgetItem;
class QModelIndex;
class QAction;
class QActionGroup;
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

    // ---- Bookmarks (per-video timestamp markers) ----
    void onAddBookmark();                              // Ctrl+B / button
    void onBookmarkActivated(QListWidgetItem* item);   // double-click → jump
    void onRemoveBookmark();                           // 삭제 button / context menu

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

    // ---- Volume (keyboard shortcut) ----
    void onVolumeUp();
    void onVolumeDown();

    // ---- Thumbnail ----
    void onTakeScreenshot();
    void onImportThumbnail();
    void onAutoThumbnail();

    // ---- Tools / Options ----
    void onOptionsTriggered();

    // ---- View ----
    void onToggleFullscreen();
    void onExitFullscreen();    // Esc — no-op when not fullscreen
    void onToggleAlwaysOnTop(bool on);   // 항상 위 — keep window above others
    void onToggleMiniPlayer(bool on);    // 미니 플레이어 — compact PiP mode

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
    QLabel*          m_fsTitleLabel{nullptr};  // top OSD title overlay (fullscreen only)
    QByteArray       m_savedGeometry;   // geometry snapshot before entering fullscreen
    QTimer*          m_fsHideTimer{nullptr};  // auto-hide controls overlay in fullscreen

    // ---- Always-on-top + mini player (PiP) ----
    // m_alwaysOnTop mirrors the user's "항상 위" choice (persisted). Mini mode
    // forces on-top regardless; on leaving mini we restore on-top to exactly
    // this value so the user's standalone preference isn't stranded.
    // m_preMiniGeometry snapshots geometry before entering mini so toggling
    // off restores the previous window size/position. m_preMiniOnTop records
    // the on-top state at mini-entry for the same restore-cleanly reason.
    bool             m_alwaysOnTop{false};
    bool             m_miniMode{false};
    bool             m_preMiniOnTop{false};
    QByteArray       m_preMiniGeometry;

    // ---- Left panel widgets ----
    QLineEdit*       m_searchEdit{nullptr};
    QSpinBox*        m_minRatingSpinBox{nullptr};
    QListView*       m_playlistView{nullptr};
    ThumbnailLabel*  m_thumbnailLabel{nullptr};
    QSpinBox*        m_ratingSpinBox{nullptr};
    QTextEdit*   m_memoEdit{nullptr};
    QPushButton* m_saveButton{nullptr};
    // ---- Bookmarks ----
    QListWidget* m_bookmarkList{nullptr};
    QPushButton* m_addBookmarkButton{nullptr};
    QPushButton* m_removeBookmarkButton{nullptr};
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
    QAction* m_actVolumeUp{nullptr};
    QAction* m_actVolumeDown{nullptr};
    QAction* m_actPrevious{nullptr};
    QAction* m_actNext{nullptr};
    QAction* m_actAddFiles{nullptr};
    QAction* m_actAddFolder{nullptr};
    QAction* m_actRemoveSelected{nullptr};
    QAction* m_actSaveMeta{nullptr};
    QAction* m_actAddBookmark{nullptr};
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
    QAction* m_actAlwaysOnTop{nullptr};   // checkable — 항상 위
    QAction* m_actMiniPlayer{nullptr};    // checkable — 미니 플레이어
    QAction* m_actOptions{nullptr};
    QAction* m_actQuit{nullptr};

    // ---- Video adjustment actions ----
    // Aspect ratio is a QActionGroup of checkable entries (auto / 16:9 /
    // 4:3 / 1.85:1 / 2.35:1). Rotate cycles +90°, zoom in/out/reset are
    // transient nudges, deinterlace is a checkable toggle, and reset
    // restores every adjustment to its default.
    QActionGroup* m_aspectGroup{nullptr};
    QAction* m_actRotate{nullptr};
    QAction* m_actZoomIn{nullptr};
    QAction* m_actZoomOut{nullptr};
    QAction* m_actZoomReset{nullptr};
    QAction* m_actDeinterlace{nullptr};
    QAction* m_actResetVideoAdj{nullptr};

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

    // ---- Metadata-pane dirty tracking ----
    // Flips on when the user edits rating/memo; stays on until they hit
    // Save OR we auto-flush on a row switch / window close. Without this a
    // half-typed memo vanishes when focus moves to another video.
    // m_metaDirtyForId pins the dirty state to the entry being edited so a
    // late currentChanged signal can't redirect the save to the wrong row.
    // m_suppressMetaDirty guards the programmatic populate in loadCurrentItem.
    bool                     m_metaDirty{false};
    int                      m_metaDirtyForId{0};
    bool                     m_suppressMetaDirty{false};

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

    // ---- Seek-bar hover thumbnail preview ----
    // Frameless top-level popup shown above the seek slider while the mouse
    // hovers it: a cropped cell from the current video's index sheet plus a
    // MM:SS caption. m_seekPreviewSheet caches the loaded index-sheet pixmap
    // (keyed by m_seekPreviewSheetPath) so we don't re-read it from disk on
    // every mouse-move; it's refreshed when playback starts (playItemAtRow)
    // or whenever the playing item's thumbnailPath changes. The preview is
    // hover-only and never interferes with click/drag seeking.
    QLabel*  m_seekPreview{nullptr};
    QPixmap  m_seekPreviewSheet;
    QString  m_seekPreviewSheetPath;

    // Build the popup lazily and refresh the cached index sheet for the
    // currently-playing item. Safe to call repeatedly.
    void     ensureSeekPreview();
    void     refreshSeekPreviewSheet();
    // Update + show the popup for a hovered slider x (local to the slider).
    void     showSeekPreviewAt(int sliderX);

    // ---- Setup helpers ----
    void     setupDatabase();
    void     setupUI();
    void     setupConnections();
    void     createActions();
    void     setupMenuBar();
    void     loadShortcuts();
    void     saveShortcuts();

    // Re-apply the persisted video adjustments (aspect / rotate /
    // deinterlace) to the mpv widget. Called after a file loads, since
    // mpv keeps these per-instance and a fresh file inherits whatever was
    // last set — re-applying guarantees the user's saved choice sticks
    // even if some property got reset during load. Zoom/pan are transient
    // (reset per file) so they're not touched here.
    void     applyVideoAdjustments();
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
    // Auto-flush pending rating/memo edits before the dirty state is
    // overwritten by a new selection or the window closes. No-op when clean.
    void flushPendingMetaEdits();
    void updateThumbnailDisplay(const QString& path);

    // ---- Bookmarks ----
    // Reload the bookmark list for the currently SELECTED item
    // (m_currentItem). No-op (clears the list) when nothing is selected.
    // Each row stores its bookmark id in Qt::UserRole and its position in
    // Qt::UserRole+1 so the jump/delete handlers can read them back.
    void refreshBookmarks();

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
    // Position the fullscreen title OSD at top-center of the video area,
    // mirroring positionFsControlsBar(). Called from the same places.
    void positionFsTitle();
    // Refresh m_fsTitleLabel's text from the currently-playing item
    // (m_playingItem → m_currentItem → file name). Called on playback start
    // and when entering fullscreen so the OSD is always current.
    void updateFsTitle();

    // Qt event filter for double-click on thumbnail label
    bool eventFilter(QObject* obj, QEvent* event) override;
};
