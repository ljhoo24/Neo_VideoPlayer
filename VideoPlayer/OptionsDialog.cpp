#include "OptionsDialog.h"

#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QMap>
#include <algorithm>   // std::clamp
#include <utility>   // std::move

// ============================================================
// Small helper — strip Qt mnemonic markers so "파일(&F)" displays as
// "파일(F)" in a plain label (not as an underline directive).
// ============================================================
static QString stripMnemonic(QString s)
{
    // Remove every single '&' but collapse "&&" → "&"
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i)
    {
        const QChar c = s[i];
        if (c == QChar('&'))
        {
            if (i + 1 < s.size() && s[i + 1] == QChar('&'))
            {
                out.append('&');
                ++i;
            }
            // else: skip single ampersand
        }
        else
        {
            out.append(c);
        }
    }
    return out;
}

// ============================================================
// Constructor
// ============================================================

OptionsDialog::OptionsDialog(QList<QAction*> actions, QWidget* parent)
    : QDialog(parent)
    , m_actions(std::move(actions))
{
    setWindowTitle("옵션");
    setMinimumSize(800, 600);
    resize(900, 700);

    // Snapshot the defaults before the user touches anything — so the
    // "기본값 복원" button actually has something to restore to.
    m_defaults.reserve(m_actions.size());
    for (QAction* a : m_actions)
        m_defaults.append(a->shortcut());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs, 1);

    buildShortcutTab();
    buildGeneralTab();

    // ---- Button row ----
    auto* btnRow = new QHBoxLayout;

    auto* resetBtn = new QPushButton("기본값 복원", this);
    resetBtn->setToolTip("모든 단축키를 프로그램 기본값으로 되돌립니다");
    connect(resetBtn, &QPushButton::clicked,
            this, &OptionsDialog::onResetDefaults);
    btnRow->addWidget(resetBtn);

    btnRow->addStretch();

    auto* dbb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(dbb, &QDialogButtonBox::accepted,
            this, &OptionsDialog::onAccepted);
    connect(dbb, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    btnRow->addWidget(dbb);

    root->addLayout(btnRow);
}

// ============================================================
// Tabs
// ============================================================

void OptionsDialog::buildShortcutTab()
{
    auto* page = new QWidget(m_tabs);
    auto* vl   = new QVBoxLayout(page);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(6);

    auto* hint = new QLabel(
        "단축키 셀을 클릭한 뒤 원하는 조합을 누르세요.  "
        "지우기 버튼으로 단축키를 제거할 수 있습니다.",
        page);
    hint->setWordWrap(true);
    vl->addWidget(hint);

    m_shortcutTable = new QTableWidget(page);
    m_shortcutTable->setColumnCount(2);
    m_shortcutTable->setHorizontalHeaderLabels({"기능", "단축키"});
    m_shortcutTable->verticalHeader()->setVisible(false);
    m_shortcutTable->verticalHeader()->setDefaultSectionSize(34);
    m_shortcutTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_shortcutTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_shortcutTable->setFocusPolicy(Qt::NoFocus);
    m_shortcutTable->setAlternatingRowColors(true);

    auto* hdr = m_shortcutTable->horizontalHeader();
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    hdr->setSectionResizeMode(1, QHeaderView::Fixed);
    m_shortcutTable->setColumnWidth(1, 280);

    vl->addWidget(m_shortcutTable, 1);

    m_tabs->addTab(page, "단축키");

    populateTable();
}

void OptionsDialog::buildGeneralTab()
{
    auto* page = new QWidget(m_tabs);
    auto* vl   = new QVBoxLayout(page);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(10);

    // ---- 업스케일 그룹 ----
    auto* upGroup = new QGroupBox("업스케일", page);
    auto* upForm  = new QFormLayout(upGroup);
    upForm->setContentsMargins(10, 12, 10, 12);
    upForm->setSpacing(8);

    // NIS Sharpness slider.
    //
    // The NIS shader's SHARPNESS define accepts 0.0..1.0 (see top of
    // NVScaler.glsl). We expose 0..100 on the integer slider so the
    // user can hit every percentage point, then divide by 100 when
    // we hand the value back to MainWindow.
    m_nisSharpnessSlider = new QSlider(Qt::Horizontal, upGroup);
    m_nisSharpnessSlider->setRange(0, 100);
    m_nisSharpnessSlider->setSingleStep(1);
    m_nisSharpnessSlider->setPageStep(10);
    m_nisSharpnessSlider->setTickPosition(QSlider::TicksBelow);
    m_nisSharpnessSlider->setTickInterval(10);
    m_nisSharpnessSlider->setToolTip(
        "NVIDIA NIS 모드에서 적용되는 적응형 샤프닝 강도입니다.\n"
        "0.0 = 샤프닝 거의 없음 / 1.0 = 최대.  기본값 0.5.\n"
        "다른 업스케일 모드에서는 영향이 없습니다.");

    m_nisSharpnessValueLabel = new QLabel(upGroup);
    m_nisSharpnessValueLabel->setMinimumWidth(48);
    m_nisSharpnessValueLabel->setAlignment(
        Qt::AlignRight | Qt::AlignVCenter);

    // Live label update — also keeps m_nisSharpness current so the
    // OK handler can read it without re-querying the slider.
    connect(m_nisSharpnessSlider, &QSlider::valueChanged,
            this, [this](int v) {
                m_nisSharpness = v / 100.0;
                m_nisSharpnessValueLabel->setText(
                    QString::number(m_nisSharpness, 'f', 2));
            });

    // Seed initial widget state from the (default-constructed or
    // caller-provided) m_nisSharpness.
    const int initial = static_cast<int>(
        std::clamp(m_nisSharpness, 0.0, 1.0) * 100.0 + 0.5);
    m_nisSharpnessSlider->setValue(initial);
    m_nisSharpnessValueLabel->setText(
        QString::number(initial / 100.0, 'f', 2));

    auto* sharpRow = new QHBoxLayout;
    sharpRow->setSpacing(8);
    sharpRow->addWidget(m_nisSharpnessSlider, 1);
    sharpRow->addWidget(m_nisSharpnessValueLabel);

    upForm->addRow("NIS 샤프닝 강도", sharpRow);

    auto* hint = new QLabel(
        "* NIS 모드일 때만 적용되며 OK 누르면 즉시 반영됩니다.",
        upGroup);
    hint->setStyleSheet("color: gray;");
    upForm->addRow(hint);

    vl->addWidget(upGroup);

    // ---- 재생 그룹 ----
    auto* playGroup = new QGroupBox("재생", page);
    auto* playForm  = new QVBoxLayout(playGroup);
    playForm->setContentsMargins(10, 12, 10, 12);
    playForm->setSpacing(8);

    // "이어보기" — when enabled, each video resumes from where it was last
    // left off. Live state is mirrored into m_resumeEnabled so the OK
    // handler can read it without re-querying the widget.
    m_resumeCheck = new QCheckBox(
        "이어보기 (마지막 위치에서 이어서 재생)", playGroup);
    m_resumeCheck->setToolTip(
        "켜면 각 영상을 이전에 멈춘 위치에서 다시 재생합니다.\n"
        "끄면 항상 처음부터 재생합니다.");
    m_resumeCheck->setChecked(m_resumeEnabled);
    connect(m_resumeCheck, &QCheckBox::toggled,
            this, [this](bool on) { m_resumeEnabled = on; });
    playForm->addWidget(m_resumeCheck);

    vl->addWidget(playGroup);

    // ---- 재생목록 그룹 ----
    auto* listGroup = new QGroupBox("재생목록", page);
    auto* listForm  = new QFormLayout(listGroup);
    listForm->setContentsMargins(10, 12, 10, 12);
    listForm->setSpacing(8);

    // 표시 방식 — 리스트(제목만) vs 썸네일 그리드. Index 0 = 리스트,
    // index 1 = 그리드, mirrored into m_playlistGridMode so onAccepted /
    // the caller can read it back without re-querying the combo.
    m_playlistViewCombo = new QComboBox(listGroup);
    m_playlistViewCombo->addItem("리스트");   // index 0 → grid = false
    m_playlistViewCombo->addItem("그리드");   // index 1 → grid = true
    m_playlistViewCombo->setToolTip(
        "재생목록을 제목만 나열하는 리스트, 또는 썸네일을 보여주는\n"
        "그리드 형태로 표시합니다. OK 누르면 즉시 반영됩니다.");
    m_playlistViewCombo->setCurrentIndex(m_playlistGridMode ? 1 : 0);
    connect(m_playlistViewCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) { m_playlistGridMode = (idx == 1); });
    listForm->addRow("표시 방식", m_playlistViewCombo);

    vl->addWidget(listGroup);

    // ---- 테마 그룹 ----
    auto* themeGroup = new QGroupBox("테마", page);
    auto* themeForm  = new QFormLayout(themeGroup);
    themeForm->setContentsMargins(10, 12, 10, 12);
    themeForm->setSpacing(8);

    // 테마 모드 — index 0 = 다크, 1 = 라이트, 2 = 자동(시스템). Mirrored
    // into m_themeMode so onAccepted / the caller can read it back.
    m_themeCombo = new QComboBox(themeGroup);
    m_themeCombo->addItem("다크");          // 0
    m_themeCombo->addItem("라이트");        // 1
    m_themeCombo->addItem("자동 (시스템)");  // 2
    m_themeCombo->setToolTip(
        "다크 / 라이트, 또는 자동(시스템)을 선택합니다.\n"
        "자동은 Windows 앱 모드 설정을 따라가며, 변경 시 즉시 반영됩니다.");
    m_themeCombo->setCurrentIndex(m_themeMode);
    connect(m_themeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) { m_themeMode = idx; });
    themeForm->addRow("모드", m_themeCombo);

    // 강조 색 — opens a QColorDialog; the button's background shows the
    // current choice so the swatch doubles as a preview.
    m_accentButton = new QPushButton(themeGroup);
    m_accentButton->setToolTip(
        "버튼·슬라이더·선택 항목에 쓰이는 강조 색입니다. 클릭하여 변경하세요.");
    m_accentButton->setFixedSize(64, 26);
    connect(m_accentButton, &QPushButton::clicked, this, [this] {
        const QColor picked = QColorDialog::getColor(
            m_accentColor, this, "강조 색 선택");
        if (picked.isValid())
        {
            m_accentColor = picked;
            updateAccentSwatch();
        }
    });
    updateAccentSwatch();
    themeForm->addRow("강조 색", m_accentButton);

    vl->addWidget(themeGroup);

    // ---- 오디오 그룹 ----
    // 음량 정규화 체크 + 저음/고음/부스트 게인 슬라이더(각 -12..+12 dB, 0 = 평탄).
    // 모든 값은 m_audio* 멤버에 미러링되어 onAccepted / 호출자가 다시 위젯을
    // 조회하지 않고 읽을 수 있다. OK 누르면 MainWindow가 mpv "af" 체인에 반영.
    auto* audioGroup = new QGroupBox("오디오", page);
    auto* audioForm  = new QFormLayout(audioGroup);
    audioForm->setContentsMargins(10, 12, 10, 12);
    audioForm->setSpacing(8);

    m_audioNormalizeCheck = new QCheckBox(
        "음량 정규화 (라우드니스 자동 평준화)", audioGroup);
    m_audioNormalizeCheck->setToolTip(
        "켜면 재생 중 음량을 실시간으로 평준화합니다 (ffmpeg dynaudnorm).\n"
        "장면별 음량 편차가 큰 영상에서 유용합니다.");
    m_audioNormalizeCheck->setChecked(m_audioNormalize);
    connect(m_audioNormalizeCheck, &QCheckBox::toggled,
            this, [this](bool on) { m_audioNormalize = on; });
    audioForm->addRow(m_audioNormalizeCheck);

    // 저음/고음/부스트 슬라이더를 같은 패턴으로 만든다. 슬라이더는 dB 값을
    // 그대로(-12..+12) 들고, 옆 라벨에 "+N dB" 형식으로 표시한다. valueChanged
    // 람다가 대응되는 m_*Gain 멤버와 라벨을 함께 갱신한다.
    auto makeGainRow =
        [this, audioGroup, audioForm](const QString& label,
                                      QSlider*& slider,
                                      QLabel*&  valueLabel,
                                      int&      mirror)
    {
        slider = new QSlider(Qt::Horizontal, audioGroup);
        slider->setRange(-12, 12);
        slider->setSingleStep(1);
        slider->setPageStep(3);
        slider->setTickPosition(QSlider::TicksBelow);
        slider->setTickInterval(3);

        valueLabel = new QLabel(audioGroup);
        valueLabel->setMinimumWidth(48);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        int* mirrorPtr      = &mirror;
        QLabel* labelPtr    = valueLabel;
        auto formatDb = [](int v) {
            return QString("%1%2 dB")
                .arg(v > 0 ? "+" : "")   // 음수는 자체 부호, 양수만 '+' 추가
                .arg(v);
        };
        connect(slider, &QSlider::valueChanged, this,
                [mirrorPtr, labelPtr, formatDb](int v) {
                    *mirrorPtr = v;
                    labelPtr->setText(formatDb(v));
                });

        slider->setValue(mirror);          // seed from current mirror
        valueLabel->setText(formatDb(mirror));

        auto* rowL = new QHBoxLayout;
        rowL->setSpacing(8);
        rowL->addWidget(slider, 1);
        rowL->addWidget(valueLabel);
        audioForm->addRow(label, rowL);
    };

    makeGainRow("저음",   m_bassSlider,   m_bassValueLabel,   m_bassGain);
    makeGainRow("고음",   m_trebleSlider, m_trebleValueLabel, m_trebleGain);
    makeGainRow("부스트", m_preampSlider, m_preampValueLabel, m_preampGain);

    auto* audioHint = new QLabel(
        "* 각 게인은 -12 ~ +12 dB, 0 = 평탄. OK 누르면 즉시 반영됩니다.",
        audioGroup);
    audioHint->setStyleSheet("color: gray;");
    audioForm->addRow(audioHint);

    vl->addWidget(audioGroup);
    vl->addStretch(1);

    m_tabs->addTab(page, "일반");
}

// Paint the accent button as a solid swatch of the current colour. A
// contrasting border keeps a very light/dark swatch visible against the
// dialog background.
void OptionsDialog::updateAccentSwatch()
{
    if (!m_accentButton)
        return;
    m_accentButton->setStyleSheet(
        QString("QPushButton { background-color: %1; border: 1px solid #808080; "
                "border-radius: 4px; }")
            .arg(m_accentColor.name()));
}

void OptionsDialog::setThemeMode(int mode)
{
    m_themeMode = qBound(0, mode, 2);
    if (m_themeCombo)
        m_themeCombo->setCurrentIndex(m_themeMode);
}

void OptionsDialog::setAccentColor(const QColor& color)
{
    if (!color.isValid())
        return;
    m_accentColor = color;
    updateAccentSwatch();
}

void OptionsDialog::setPlaylistGridMode(bool grid)
{
    m_playlistGridMode = grid;
    if (m_playlistViewCombo)
        m_playlistViewCombo->setCurrentIndex(grid ? 1 : 0);
}

void OptionsDialog::setNisSharpness(double value)
{
    m_nisSharpness = std::clamp(value, 0.0, 1.0);
    if (m_nisSharpnessSlider)
    {
        // setValue triggers valueChanged which updates the label,
        // so we don't need to touch m_nisSharpnessValueLabel directly.
        m_nisSharpnessSlider->setValue(
            static_cast<int>(m_nisSharpness * 100.0 + 0.5));
    }
}

void OptionsDialog::setResumeEnabled(bool enabled)
{
    m_resumeEnabled = enabled;
    if (m_resumeCheck)
        m_resumeCheck->setChecked(enabled);   // toggled() keeps the flag in sync
}

// ---- 오디오 ----
// 각 setter는 미러 멤버를 갱신하고, 위젯이 이미 만들어졌으면 위젯도 맞춘다.
// 슬라이더 setValue / 체크 setChecked 는 대응 valueChanged/toggled 람다를
// 호출하므로 미러와 라벨이 함께 동기화된다.

void OptionsDialog::setAudioNormalize(bool on)
{
    m_audioNormalize = on;
    if (m_audioNormalizeCheck)
        m_audioNormalizeCheck->setChecked(on);
}

void OptionsDialog::setBassGain(int dB)
{
    m_bassGain = std::clamp(dB, -12, 12);
    if (m_bassSlider)
        m_bassSlider->setValue(m_bassGain);
}

void OptionsDialog::setTrebleGain(int dB)
{
    m_trebleGain = std::clamp(dB, -12, 12);
    if (m_trebleSlider)
        m_trebleSlider->setValue(m_trebleGain);
}

void OptionsDialog::setPreampGain(int dB)
{
    m_preampGain = std::clamp(dB, -12, 12);
    if (m_preampSlider)
        m_preampSlider->setValue(m_preampGain);
}

// ============================================================
// Table population  (row index == index in m_actions / m_defaults)
// ============================================================

void OptionsDialog::populateTable()
{
    m_shortcutTable->setRowCount(m_actions.size());

    for (int i = 0; i < m_actions.size(); ++i)
    {
        QAction* a = m_actions[i];

        const QString name = stripMnemonic(a->text());

        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setFlags(Qt::ItemIsEnabled);
        nameItem->setToolTip(a->objectName());     // internal id for clarity
        m_shortcutTable->setItem(i, 0, nameItem);

        auto* edit = new QKeySequenceEdit(a->shortcut(), m_shortcutTable);
        edit->setClearButtonEnabled(true);
        m_shortcutTable->setCellWidget(i, 1, edit);
    }
}

// ============================================================
// Slot — OK pressed
// ============================================================

void OptionsDialog::onAccepted()
{
    // ---- Duplicate-shortcut guard ----
    QMap<QString, QString> seen;   // keyStr → action display name
    for (int i = 0; i < m_actions.size(); ++i)
    {
        auto* edit = qobject_cast<QKeySequenceEdit*>(
            m_shortcutTable->cellWidget(i, 1));
        if (!edit)
            continue;

        const QKeySequence seq = edit->keySequence();
        if (seq.isEmpty())
            continue;

        const QString keyStr  = seq.toString(QKeySequence::PortableText);
        const QString display = stripMnemonic(m_actions[i]->text());

        if (seen.contains(keyStr))
        {
            QMessageBox::warning(this, "단축키 충돌",
                QString("'%1' 단축키가 '%2' 과(와) '%3' 항목에 중복 할당되었습니다.\n\n"
                        "한 쪽을 변경하거나 지운 뒤 다시 시도하세요.")
                    .arg(keyStr, seen.value(keyStr), display));
            return;
        }
        seen.insert(keyStr, display);
    }

    // ---- Apply ----
    for (int i = 0; i < m_actions.size(); ++i)
    {
        auto* edit = qobject_cast<QKeySequenceEdit*>(
            m_shortcutTable->cellWidget(i, 1));
        if (!edit)
            continue;
        m_actions[i]->setShortcut(edit->keySequence());
    }

    accept();
}

// ============================================================
// Slot — Reset defaults
// ============================================================

void OptionsDialog::onResetDefaults()
{
    const auto ans = QMessageBox::question(
        this, "기본값 복원",
        "단축키, 테마, 강조 색을 모두 기본값으로 되돌릴까요?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes)
        return;

    // 단축키
    for (int i = 0; i < m_actions.size() && i < m_defaults.size(); ++i)
    {
        auto* edit = qobject_cast<QKeySequenceEdit*>(
            m_shortcutTable->cellWidget(i, 1));
        if (edit)
            edit->setKeySequence(m_defaults[i]);
    }

    // 테마 → 다크(기본), 강조 색 → 기본 파랑(#4f93ff). 콤보를 통해 설정해
    // m_themeMode 미러링도 함께 갱신되게 한다. (OK 누를 때 실제 반영)
    setThemeMode(0);                         // 다크
    setAccentColor(QColor(0x4f, 0x93, 0xff)); // 기본 강조 색 + 스와치 갱신
}
