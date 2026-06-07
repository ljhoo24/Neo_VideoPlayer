#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QList>

// Forward-declare Qt classes we only use by pointer
class QAction;
class QTableWidget;
class QTabWidget;
class QSlider;
class QLabel;
class QCheckBox;
class QComboBox;

// ============================================================
// OptionsDialog
//   Tabbed configuration window.  Currently hosts:
//     1. "일반"   — reserved for future preferences
//     2. "단축키" — per-QAction shortcut editor
//
//   The dialog is given a list of QAction* (the "customisable" subset
//   owned by MainWindow).  On accept, the edited QKeySequence values
//   are pushed back into those actions.  MainWindow then persists the
//   result with QSettings.
//
//   Sized large on purpose — the user plans to keep adding more
//   shortcuts; 900×700 leaves breathing room.
// ============================================================
class OptionsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QList<QAction*> actions, QWidget* parent = nullptr);
    ~OptionsDialog() override = default;

    // Non-copyable
    OptionsDialog(const OptionsDialog&)            = delete;
    OptionsDialog& operator=(const OptionsDialog&) = delete;

    // ---- "일반" tab — playback options ----
    //
    // NIS sharpening strength, 0.0 .. 1.0. The slider stores in units
    // of 0.01 (range 0..100) so the integer slider can hit every step
    // the shader cares about. Caller seeds the initial value before
    // exec() and reads back the final value after Accepted.
    void   setNisSharpness(double value);
    double nisSharpness() const noexcept { return m_nisSharpness; }

    // "이어보기" — resume each video from its last position. Caller seeds
    // the initial state before exec() and reads it back after Accepted.
    void setResumeEnabled(bool enabled);
    bool resumeEnabled() const noexcept { return m_resumeEnabled; }

    // 재생목록 표시 방식 — false = 리스트, true = 썸네일 그리드. Caller seeds
    // the current mode before exec() and reads it back after Accepted.
    void setPlaylistGridMode(bool grid);
    bool playlistGridMode() const noexcept { return m_playlistGridMode; }

private slots:
    void onAccepted();
    void onResetDefaults();

private:
    QList<QAction*>     m_actions;    // parallel to m_defaults / table rows
    QList<QKeySequence> m_defaults;   // snapshot taken at construction
    QTabWidget*         m_tabs{nullptr};
    QTableWidget*       m_shortcutTable{nullptr};

    // General tab widgets
    QSlider* m_nisSharpnessSlider{nullptr};
    QLabel*  m_nisSharpnessValueLabel{nullptr};
    double   m_nisSharpness{0.5};

    QCheckBox* m_resumeCheck{nullptr};
    bool       m_resumeEnabled{true};   // default: enabled

    QComboBox* m_playlistViewCombo{nullptr};
    bool       m_playlistGridMode{false};   // default: 리스트

    void buildShortcutTab();
    void buildGeneralTab();
    void populateTable();
};
