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
    vl->addStretch(1);

    m_tabs->addTab(page, "일반");
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
        "모든 단축키를 기본값으로 되돌릴까요?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes)
        return;

    for (int i = 0; i < m_actions.size() && i < m_defaults.size(); ++i)
    {
        auto* edit = qobject_cast<QKeySequenceEdit*>(
            m_shortcutTable->cellWidget(i, 1));
        if (edit)
            edit->setKeySequence(m_defaults[i]);
    }
}
