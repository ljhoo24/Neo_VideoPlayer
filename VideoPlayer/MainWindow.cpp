#include "MainWindow.h"

#include "MpvPlayerWidget.h"
#include "OptionsDialog.h"
#include "IconFont.h"

#include <QApplication>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListView>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QAbstractSpinBox>
#include <QTextEdit>
#include <QSlider>
#include <QComboBox>
#include <QGroupBox>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QPixmap>
#include <QTimer>
#include <QMessageBox>
#include <QSizePolicy>
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QScrollArea>
#include <QScrollBar>
#include <QFile>
#include <QStatusBar>
#include <QSqlDatabase>
#include <QAction>
#include <QActionGroup>
#include <QMenuBar>
#include <QMenu>
#include <QKeySequence>
#include <QSettings>
#include <QProxyStyle>
#include <QStyleOptionSlider>
#include <QResizeEvent>
#include <QDialog>
#include <QKeyEvent>
#include <QEventLoop>
#include <QDebug>
#include <QPainter>
#include <QImage>
#include <QFont>
#include <QFontMetrics>
#include <QPointF>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

// ============================================================
// File-scope helpers
// ============================================================

// ------------------------------------------------------------
// AbsoluteSliderStyle
//   QSlider's default behaviour for a left-click on the groove is
//   "add/subtract one pageStep", not "jump to the clicked position".
//   Flipping SH_Slider_AbsoluteSetButtons to include Qt::LeftButton
//   makes left-click teleport the handle to the click position —
//   exactly the "click the bar to seek here" behaviour the user asked
//   for.  Applied to m_seekSlider only.
//
//   Note: the hint name changed in Qt 6 from the old
//   SH_Slider_AbsoluteSetButtonControl (singular) to
//   SH_Slider_AbsoluteSetButtons (plural) — the latter is what Qt
//   6.8.3 ships.
// ------------------------------------------------------------
class AbsoluteSliderStyle final : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(QStyle::StyleHint      hint,
                  const QStyleOption*    opt,
                  const QWidget*         w,
                  QStyleHintReturn*      ret) const override
    {
        if (hint == QStyle::SH_Slider_AbsoluteSetButtons)
            return Qt::LeftButton;
        return QProxyStyle::styleHint(hint, opt, w, ret);
    }
};

// ------------------------------------------------------------
// ThumbnailLabel
//   Thin QLabel subclass that keeps the "source" pixmap (the one
//   loaded from disk, at native resolution) and re-scales it on every
//   resize.  Without this, setPixmap(px.scaled(size(), …)) is a
//   one-shot — later resizes leave a too-small or cropped image.
//
//   Intentionally NO Q_OBJECT — no new signals/slots/properties, so
//   we don't need a MOC pass on MainWindow.cpp.
// ------------------------------------------------------------
class ThumbnailLabel final : public QLabel
{
public:
    using QLabel::QLabel;

    void setSourcePixmap(const QPixmap& p)
    {
        m_source = p;
        rescale();
    }

    void clearSource()
    {
        m_source = QPixmap();
        QLabel::setPixmap({});
    }

    [[nodiscard]] bool hasSource() const noexcept
    {
        return !m_source.isNull();
    }

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QLabel::resizeEvent(e);
        rescale();
    }

private:
    void rescale()
    {
        if (m_source.isNull() || size().isEmpty())
            return;
        QLabel::setPixmap(m_source.scaled(
            size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    QPixmap m_source;
};

// ------------------------------------------------------------
// ImageViewerDialog
//   Simple borderless-ish top-level window that shows a pixmap at the
//   largest size the window can accommodate.  Behaviour:
//     - starts large (1200×800), the user can resize freely
//     - image is rescaled on every resize with upscaling allowed, so
//       small source images fill the window
//     - mouse wheel zooms in / out — anchored on the cursor position,
//       so the pixel under the mouse stays under the mouse during the
//       zoom (huge UX win over the naive center-zoom)
//     - left-button drag pans the image while zoomed in (image larger
//       than viewport). Cursor switches to OpenHand on hover and
//       ClosedHand while dragging.
//     - Esc closes (QDialog default handles it via keyPressEvent)
//     - Qt::Window flag gives full min/max/close window chrome on
//       Windows — otherwise QDialog only gets a plain close button
//   No Q_OBJECT needed (no new signals/slots).
// ------------------------------------------------------------
class ImageViewerDialog final : public QDialog
{
public:
    ImageViewerDialog(const QPixmap& pixmap,
                      const QString& title,
                      QWidget*       parent = nullptr)
        : QDialog(parent), m_source(pixmap)
    {
        setWindowTitle(title.isEmpty() ? QStringLiteral("이미지 미리보기") : title);
        setWindowFlags(Qt::Window
                       | Qt::WindowTitleHint
                       | Qt::WindowSystemMenuHint
                       | Qt::WindowMinMaxButtonsHint
                       | Qt::WindowCloseButtonHint);
        resize(1200, 800);
        setMinimumSize(320, 240);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setAlignment(Qt::AlignCenter);
        m_scrollArea->setStyleSheet("QScrollArea { background:#000; border:none; }");
        m_scrollArea->viewport()->setStyleSheet("background:#000;");
        m_scrollArea->setWidgetResizable(false);

        m_label = new QLabel(this);
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setStyleSheet("background:#000;");
        m_scrollArea->setWidget(m_label);

        layout->addWidget(m_scrollArea);

        // Intercept wheel + mouse events on the viewport so we can
        // (a) zoom anchored at the cursor, (b) pan on left-drag, and
        // (c) update the cursor shape to advertise pan availability.
        m_scrollArea->viewport()->installEventFilter(this);
        m_scrollArea->viewport()->setMouseTracking(true);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* e) override
    {
        if (obj != m_scrollArea->viewport())
            return QDialog::eventFilter(obj, e);

        switch (e->type())
        {
        case QEvent::Wheel:
        {
            auto* we = static_cast<QWheelEvent*>(e);
            // Anchor zoom on the cursor position inside the viewport.
            // position() is in viewport coordinates already (Qt6).
            applyZoomAt(we->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15,
                        we->position().toPoint());
            return true;
        }

        case QEvent::MouseButtonPress:
        {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton && canPan())
            {
                m_panning      = true;
                m_panStart     = me->pos();
                m_scrollHStart = m_scrollArea->horizontalScrollBar()->value();
                m_scrollVStart = m_scrollArea->verticalScrollBar()->value();
                m_scrollArea->viewport()->setCursor(Qt::ClosedHandCursor);
                return true;
            }
            break;
        }

        case QEvent::MouseMove:
        {
            auto* me = static_cast<QMouseEvent*>(e);
            if (m_panning)
            {
                // Translate the scroll bars by the inverse of the
                // pointer delta — the image "follows the finger".
                // Using setValue (not setSliderPosition) so the change
                // is immediate, no animation/snapping latency.
                const QPoint d = me->pos() - m_panStart;
                m_scrollArea->horizontalScrollBar()
                    ->setValue(m_scrollHStart - d.x());
                m_scrollArea->verticalScrollBar()
                    ->setValue(m_scrollVStart - d.y());
                return true;
            }
            // Not dragging — just keep the cursor shape honest as the
            // mouse glides over zoomed-in vs zoomed-out areas.
            updatePanCursor();
            break;
        }

        case QEvent::MouseButtonRelease:
        {
            auto* me = static_cast<QMouseEvent*>(e);
            if (m_panning && me->button() == Qt::LeftButton)
            {
                m_panning = false;
                updatePanCursor();
                return true;
            }
            break;
        }

        case QEvent::Enter:
        case QEvent::Leave:
            updatePanCursor();
            break;

        default:
            break;
        }

        return QDialog::eventFilter(obj, e);
    }

    void resizeEvent(QResizeEvent* e) override
    {
        QDialog::resizeEvent(e);
        updateDisplay();
    }

    void showEvent(QShowEvent* e) override
    {
        QDialog::showEvent(e);
        updateDisplay();
    }

private:
    // True when the image is currently larger than the viewport, i.e.
    // there's somewhere to pan to. Auto-fit (m_zoomFactor < 0) never
    // qualifies because the image is shrunk to fit by definition.
    bool canPan() const
    {
        if (m_zoomFactor < 0.0) return false;
        const QSize vp = m_scrollArea->viewport()->size();
        const QSize ls = m_label->size();
        return ls.width() > vp.width() || ls.height() > vp.height();
    }

    void updatePanCursor()
    {
        if (m_panning) return;   // ClosedHand is owned by the drag
        m_scrollArea->viewport()->setCursor(
            canPan() ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }

    // Zoom by `factor` while keeping the image pixel under
    // `viewportAnchor` glued under the cursor. Equivalent to:
    //   1. record where the cursor maps to in image space pre-zoom
    //   2. apply the zoom (rescale + relayout the label)
    //   3. shift the scroll bars so the same image-space point lands
    //      back at viewportAnchor.
    void applyZoomAt(qreal factor, QPoint viewportAnchor)
    {
        if (m_source.isNull()) return;

        // Promote auto-fit to an explicit factor on first zoom so the
        // anchor math below has a real baseline.
        if (m_zoomFactor < 0.0)
        {
            const QSize vp = m_scrollArea->viewport()->size();
            const qreal fw = qreal(vp.width())  / m_source.width();
            const qreal fh = qreal(vp.height()) / m_source.height();
            m_zoomFactor   = qMin(fw, fh);
        }

        // Convert anchor (viewport coords) → label coords → image
        // pixel coords pre-zoom. mapFrom handles the case where the
        // label is centred (smaller than viewport, large negative or
        // positive offset).
        const QPoint anchorInLabel =
            m_label->mapFrom(m_scrollArea->viewport(), viewportAnchor);

        QPointF imgPx{0.0, 0.0};
        if (m_label->width() > 0 && m_label->height() > 0)
        {
            imgPx.setX(qreal(anchorInLabel.x()) * m_source.width()
                                                / qreal(m_label->width()));
            imgPx.setY(qreal(anchorInLabel.y()) * m_source.height()
                                                / qreal(m_label->height()));
        }

        const qreal newZoom = qBound(0.05, m_zoomFactor * factor, 20.0);
        if (qFuzzyCompare(newZoom, m_zoomFactor))
            return;   // clamped — nothing to do, don't disturb scroll
        m_zoomFactor = newZoom;
        updateDisplay();

        // After rescale, where does that same image pixel sit inside
        // the *new* label?
        const QPoint newAnchorInLabel{
            qRound(imgPx.x() * m_zoomFactor),
            qRound(imgPx.y() * m_zoomFactor),
        };

        // We want newAnchorInLabel, when mapped to viewport, to equal
        // viewportAnchor. Adjust scrollbars by the difference between
        // current and desired viewport position of that label point.
        const QPoint curViewportPos =
            m_scrollArea->viewport()->mapFrom(m_label, newAnchorInLabel);
        const QPoint shift = curViewportPos - viewportAnchor;

        auto* hbar = m_scrollArea->horizontalScrollBar();
        auto* vbar = m_scrollArea->verticalScrollBar();
        hbar->setValue(hbar->value() + shift.x());
        vbar->setValue(vbar->value() + shift.y());

        updatePanCursor();
    }

    void updateDisplay()
    {
        if (m_source.isNull()) return;
        QSize sz;
        if (m_zoomFactor < 0.0) {
            // Auto-fit: scale to viewport while keeping aspect ratio.
            sz = m_source.size().scaled(
                m_scrollArea->viewport()->size(), Qt::KeepAspectRatio);
        } else {
            sz = QSize(qRound(m_source.width()  * m_zoomFactor),
                       qRound(m_source.height() * m_zoomFactor));
        }
        QPixmap scaled = m_source.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_label->setPixmap(scaled);
        m_label->resize(scaled.size());

        updatePanCursor();
    }

    QPixmap      m_source;
    QScrollArea* m_scrollArea{nullptr};
    QLabel*      m_label{nullptr};
    qreal        m_zoomFactor{-1.0};   // negative = auto-fit

    // Pan-drag state
    bool         m_panning{false};
    QPoint       m_panStart;            // viewport coord of LMB-down
    int          m_scrollHStart{0};     // scrollbar snapshot at drag start
    int          m_scrollVStart{0};
};

// ------------------------------------------------------------
// IndexSheetGenerator
//   Generates a 4×3 contact-sheet thumbnail (a.k.a. "index sheet")
//   for a video — mirroring the Python reference in
//   reference/index_sheet.py.  Output layout:
//
//     ┌─────────────────────────────────────────────────────┐
//     │ <file name>                          (header, 96 px) │
//     │ 해상도: WxH   재생시간: …   파일크기: …   코덱: …    │
//     ├──────┬──────┬──────┬──────┬──────────────────────────┤
//     │  f0  │  f1  │  f2  │  f3  │ ← 4 × 3 grid of frames   │
//     │  f4  │  f5  │  f6  │  f7  │   evenly distributed     │
//     │  f8  │  f9  │  f10 │  f11 │   between 5% and 95%     │
//     └─────────────────────────────────────────────────────┘
//
//   Frames are captured via mpv's `screenshot-to-file` command, so
//   they run through the same decoder + colour pipeline the player
//   already uses.  Capture is asynchronous (seek + screenshot are
//   queued mpv commands that need a moment to flush), so the
//   generator chains itself via QTimer::singleShot until all 12
//   frames are on disk, then composites them with QPainter and
//   saves a single JPEG.
//
//   Lifetime: the generator must outlive its async chain.  Construct
//   via std::make_shared<…> and call start() — the chain captures a
//   shared_ptr to self in every lambda, so the object stays alive
//   until the final QImage::save() completes.  The done callback is
//   invoked exactly once on either success or failure.
// ------------------------------------------------------------
class IndexSheetGenerator final
    : public std::enable_shared_from_this<IndexSheetGenerator>
{
public:
    using DoneCallback = std::function<void(bool success, const QString& outputPath)>;

    IndexSheetGenerator(MpvPlayerWidget* mpv,
                        QString          videoPath,
                        QString          outputPath,
                        DoneCallback     done)
        : m_mpv(mpv)
        , m_videoPath(std::move(videoPath))
        , m_outputPath(std::move(outputPath))
        , m_done(std::move(done))
    {}

    // Kick off the capture chain.  Returns immediately; the done
    // callback fires when the JPEG has been written (or on failure).
    void start()
    {
        m_duration = m_mpv ? m_mpv->duration() : 0.0;
        m_videoW   = m_mpv ? m_mpv->videoWidth()  : 0;
        m_videoH   = m_mpv ? m_mpv->videoHeight() : 0;
        m_codec    = m_mpv ? m_mpv->videoCodec()  : QString{};
        m_fileSize = QFileInfo(m_videoPath).size();

        if (m_duration <= 0.0 || m_videoW <= 0 || m_videoH <= 0)
        {
            qWarning() << "[IndexSheet] missing metadata — duration="
                       << m_duration << " size=" << m_videoW << "x" << m_videoH;
            finish(false);
            return;
        }

        // Per-pid temp dir so concurrent player instances don't trash
        // each other's intermediates.
        m_tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                  + QStringLiteral("/videoplayer_indexsheet_")
                  + QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(m_tempDir);

        // Pause before stepping through the frames so the user's last
        // play position isn't fighting our seeks.
        if (m_mpv && !m_mpv->isPaused())
            m_mpv->togglePause();

        captureNext();
    }

private:
    // ── layout constants — must match reference/index_sheet.py ──
    static constexpr int TARGET_WIDTH = 1920;
    static constexpr int COLS         = 4;
    static constexpr int ROWS         = 3;
    static constexpr int FRAME_COUNT  = COLS * ROWS;     // 12
    static constexpr int HEADER_H     = 96;

    // ── async capture loop ──
    void captureNext()
    {
        if (m_idx >= FRAME_COUNT)
        {
            compose();
            return;
        }

        // Evenly distributed positions inside [5 %, 95 %] of the
        // timeline — same formula as the Python reference, expressed
        // in seconds instead of frame indices.
        const double frac = 0.05 + 0.90 * static_cast<double>(m_idx)
                                       / (FRAME_COUNT - 1);
        const double pos  = m_duration * frac;
        m_timestamps.push_back(pos);

        const QString framePath = QStringLiteral("%1/frame_%2.png")
                                  .arg(m_tempDir)
                                  .arg(m_idx, 2, 10, QChar('0'));
        m_framePaths.push_back(framePath);

        if (m_mpv)
            m_mpv->seek(pos);

        // seek → wait for decoder to render the target frame →
        // screenshot → wait for file to flush → advance.
        auto self = shared_from_this();
        QTimer::singleShot(350, qApp, [self, framePath]()
        {
            if (self->m_mpv)
                self->m_mpv->takeScreenshot(framePath);

            QTimer::singleShot(450, qApp, [self]()
            {
                ++self->m_idx;
                self->captureNext();
            });
        });
    }

    // ── composite the captured frames into the final JPEG ──
    void compose()
    {
        const int cell_w   = TARGET_WIDTH / COLS;
        const int cell_h   = static_cast<int>(
            static_cast<double>(cell_w) * m_videoH / m_videoW);
        const int canvas_h = HEADER_H + ROWS * cell_h;

        QImage canvas(TARGET_WIDTH, canvas_h, QImage::Format_RGB32);
        canvas.fill(QColor(24, 24, 24));   // BG_COLOR

        QPainter p(&canvas);
        p.setRenderHint(QPainter::TextAntialiasing,      true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // ── header strip ──
        // "Malgun Gothic" is Windows' default Korean UI font — the
        // same one the Python reference picks first.  Qt falls back
        // automatically if it's missing.
        QFont titleFont(QStringLiteral("Malgun Gothic"));
        titleFont.setPixelSize(26);
        QFont metaFont(QStringLiteral("Malgun Gothic"));
        metaFont.setPixelSize(18);
        QFont stampFont(QStringLiteral("Malgun Gothic"));
        stampFont.setPixelSize(15);

        // PIL anchors text by its top-left corner; QPainter anchors
        // by the baseline.  Add the font's ascent so the visual
        // position matches the Python output pixel-for-pixel.
        const QFontMetrics titleFm(titleFont);
        const QFontMetrics metaFm (metaFont);

        p.setFont(titleFont);
        p.setPen(QColor(240, 240, 240));   // HEADER_FG
        p.drawText(QPointF(16, 10 + titleFm.ascent()),
                   QFileInfo(m_videoPath).fileName());

        p.setFont(metaFont);
        p.setPen(QColor(170, 170, 170));   // META_FG
        const QString resStr = (m_videoW > 0 && m_videoH > 0)
            ? QStringLiteral("%1×%2").arg(m_videoW).arg(m_videoH)
            : QStringLiteral("N/A");
        const QString metaLine =
            QStringLiteral("해상도: %1   재생 시간: %2   파일 크기: %3   코덱: %4")
                .arg(resStr,
                     fmtDuration(m_duration),
                     fmtSize(m_fileSize),
                     m_codec.isEmpty() ? QStringLiteral("N/A") : m_codec);
        p.drawText(QPointF(16, 52 + metaFm.ascent()), metaLine);

        // Separator line at the bottom of the header.
        p.setPen(QColor(55, 55, 55));      // SEPARATOR_FG
        p.drawLine(0, HEADER_H - 1, TARGET_WIDTH, HEADER_H - 1);

        // ── grid of frames ──
        const QFontMetrics stampFm(stampFont);
        for (int i = 0; i < FRAME_COUNT && i < m_framePaths.size(); ++i)
        {
            const int col = i % COLS;
            const int row = i / COLS;
            const int x   = col * cell_w;
            const int y   = HEADER_H + row * cell_h;

            QImage frame(m_framePaths[i]);
            if (frame.isNull())
            {
                // Missing capture — fall back to a flat dark cell so
                // the grid is still complete and the layout doesn't
                // collapse around the gap.
                QImage placeholder(cell_w, cell_h, QImage::Format_RGB32);
                placeholder.fill(QColor(40, 40, 40));
                p.drawImage(x, y, placeholder);
            }
            else
            {
                const QImage scaled = frame.scaled(
                    cell_w, cell_h,
                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                p.drawImage(x, y, scaled);
            }

            // Timestamp overlay — right-bottom anchored, with an
            // 8-direction black outline (matches PIL's _OUTLINE_OFFSETS).
            p.setFont(stampFont);
            const QString ts     = fmtDuration(m_timestamps[i]);
            const int     textW  = stampFm.horizontalAdvance(ts);
            const int     baseX  = x + cell_w - 8 - textW;
            const int     baseY  = y + cell_h - 8 - stampFm.descent();

            p.setPen(Qt::black);
            for (const auto& [dx, dy] : kOutlineOffsets)
                p.drawText(baseX + dx, baseY + dy, ts);

            p.setPen(Qt::white);
            p.drawText(baseX, baseY, ts);
        }

        p.end();

        const bool saved = canvas.save(m_outputPath, "JPEG", 92);
        if (!saved)
            qWarning() << "[IndexSheet] failed to save JPEG to" << m_outputPath;

        // Best-effort cleanup of the per-pid temp directory.
        for (const QString& f : std::as_const(m_framePaths))
            QFile::remove(f);
        QDir().rmdir(m_tempDir);

        finish(saved);
    }

    void finish(bool success)
    {
        if (m_done)
            m_done(success, m_outputPath);
    }

    // ── formatting helpers — mirror reference/index_sheet.py ──
    static QString fmtDuration(double seconds)
    {
        if (seconds < 0.0) seconds = 0.0;
        const int total = static_cast<int>(seconds);
        const int h     = total / 3600;
        const int m     = (total % 3600) / 60;
        const int s     = total % 60;
        const int cs    = static_cast<int>(std::fmod(seconds, 1.0) * 100.0);
        if (h > 0)
        {
            return QStringLiteral("%1:%2:%3.%4")
                .arg(h,  2, 10, QChar('0'))
                .arg(m,  2, 10, QChar('0'))
                .arg(s,  2, 10, QChar('0'))
                .arg(cs, 2, 10, QChar('0'));
        }
        return QStringLiteral("%1:%2.%3")
            .arg(m,  2, 10, QChar('0'))
            .arg(s,  2, 10, QChar('0'))
            .arg(cs, 2, 10, QChar('0'));
    }

    static QString fmtSize(qint64 bytes)
    {
        double v = static_cast<double>(bytes);
        const char* const units[] = { "B", "KB", "MB", "GB" };
        for (const char* unit : units)
        {
            if (v < 1024.0)
                return QString::asprintf("%.1f %s", v, unit);
            v /= 1024.0;
        }
        return QString::asprintf("%.1f TB", v);
    }

    // 8-neighbourhood used to draw a 1-pixel black outline behind
    // the timestamp text.  Same offsets as PIL's reference.
    static constexpr std::array<std::pair<int, int>, 8> kOutlineOffsets = {{
        {-1,-1}, {0,-1}, {1,-1},
        {-1, 0},         {1, 0},
        {-1, 1}, {0, 1}, {1, 1},
    }};

    // ── inputs / outputs ──
    MpvPlayerWidget* m_mpv{nullptr};
    QString          m_videoPath;
    QString          m_outputPath;
    DoneCallback     m_done;

    // ── transient state during capture ──
    QString          m_tempDir;
    QStringList      m_framePaths;
    QList<double>    m_timestamps;
    int              m_idx{0};

    // ── cached metadata (gathered once at start) ──
    int     m_videoW{0};
    int     m_videoH{0};
    double  m_duration{0.0};
    QString m_codec;
    qint64  m_fileSize{0};
};

// ------------------------------------------------------------
// Video file extension helpers
//   Single source of truth for the "is this a video?" question —
//   shared between the QFileDialog filter string and the recursive
//   folder scanner's QDirIterator name filters.  Extending playback
//   support later = add an extension here and nowhere else.
// ------------------------------------------------------------
static const QStringList& videoExtensions()
{
    static const QStringList exts = {
        "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm",
        "m4v", "ts",  "m2ts","mpg", "mpeg","3gp", "ogv"
    };
    return exts;
}

// Name filters in QDir glob form: "*.mp4", "*.mkv", ...
// QDirIterator matches these case-insensitively on Windows, so
// "Foo.MP4" will match "*.mp4".
static QStringList videoNameFilters()
{
    QStringList out;
    out.reserve(videoExtensions().size());
    for (const QString& e : videoExtensions())
        out << (QStringLiteral("*.") + e);
    return out;
}

// Assembled filter string for QFileDialog.  Kept as a helper so the
// dialog filter and the recursive scan always agree on the extension
// list.
static QString videoFileDialogFilter()
{
    QStringList globs;
    globs.reserve(videoExtensions().size());
    for (const QString& e : videoExtensions())
        globs << (QStringLiteral("*.") + e);

    return QStringLiteral("Video Files (") + globs.join(QChar(' '))
         + QStringLiteral(");;All Files (*)");
}

// Return the first existing "<videoBase>_index.<ext>" sibling of
// videoPath, or an empty string if none is found.
//
//   Input:   C:/foo/bar/iam.mp4
//   Looks for:
//     C:/foo/bar/iam_index.jpg   (preferred — the ".jpg" hint the
//                                   user explicitly mentioned)
//     C:/foo/bar/iam_index.jpeg
//     C:/foo/bar/iam_index.png
//     C:/foo/bar/iam_index.bmp
//     C:/foo/bar/iam_index.webp
//
// "completeBaseName" handles multi-dot filenames correctly
// (e.g. "movie.2023.1080p.mkv" → "movie.2023.1080p").
static QString findIndexSheetFor(const QString& videoPath)
{
    const QFileInfo fi(videoPath);
    const QString base =
        fi.absolutePath() + QLatin1Char('/') +
        fi.completeBaseName() + QStringLiteral("_index");

    static constexpr const char* kExts[] =
        { ".jpg", ".jpeg", ".png", ".bmp", ".webp" };

    for (const char* ext : kExts)
    {
        const QString candidate = base + QLatin1String(ext);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}

// ============================================================
// Constructor
// ============================================================

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("VideoPlayer");
    setMinimumSize(980, 620);
    resize(1280, 720);

    setupDatabase();
    setupUI();          // UI first — m_mpvWidget must exist before createActions
    createActions();    // build QActions (and default shortcuts)
    loadShortcuts();    // override defaults with any persisted values
    setupMenuBar();     // populate menu bar with the actions
    setupConnections();

    // Restore persisted filter values BEFORE the initial playlist load.
    // Writing into the widgets fires the textChanged / valueChanged
    // signals we just connected, which propagates the values into the
    // PlaylistModel — so when refreshPlaylist() runs immediately below,
    // loadFromDatabase() + applyFilter() produce the already-filtered
    // view in one shot, instead of flashing the unfiltered list first.
    {
        QSettings s;
        const QString savedSearch = s.value("filter/searchText").toString();
        const int     savedMinRat = s.value("filter/minRating", 0).toInt();
        if (!savedSearch.isEmpty())
            m_searchEdit->setText(savedSearch);
        if (savedMinRat > 0)
            m_minRatingSpinBox->setValue(savedMinRat);

        // One-shot migration from the old boolean checkbox setting to
        // the new 3-state combo. Runs only when the new key is absent
        // *and* the old key exists — i.e. the very first launch after
        // this version is installed. Old true → Standard, old false →
        // Off. We then drop the old key so subsequent launches go
        // straight through the new code path.
        if (!s.contains("playback/upscaleMode")
            && s.contains("playback/upscaleEnabled"))
        {
            const int migrated =
                s.value("playback/upscaleEnabled").toBool() ? 1 : 0;
            s.setValue("playback/upscaleMode", migrated);
            s.remove("playback/upscaleEnabled");
            qInfo() << "[settings] migrated upscaleEnabled →"
                    << "upscaleMode =" << migrated;
        }

        // Apply saved NIS sharpness BEFORE restoring the upscale mode
        // — otherwise setCurrentIndex(NIS) triggers
        // applyUpscalingProfile with the default sharpness, then we'd
        // overwrite it a moment later. Doing sharpness first means the
        // shader is patched correctly on its first compile.
        const double savedSharp =
            s.value("playback/nisSharpness", 0.5).toDouble();
        m_mpvWidget->setNisSharpness(savedSharp);

        // Restore the upscale mode. setCurrentIndex fires
        // currentIndexChanged, which is wired to setUpscaleMode +
        // QSettings persistence — so the mpv side picks up the value
        // without an explicit call here. Index 0 is the default; only
        // bother re-setting when stored value differs, to avoid an
        // extra no-op signal during startup.
        const int savedMode =
            s.value("playback/upscaleMode", 0).toInt();
        if (savedMode > 0 && savedMode < m_upscaleCombo->count())
            m_upscaleCombo->setCurrentIndex(savedMode);
    }

    refreshPlaylist();

    // Restore the last-played entry as the current selection (no auto-
    // play — just put the cursor where the user left off so a single
    // Enter / space resumes from a familiar starting point). Saved id
    // may not be present in the current filtered view (entry deleted or
    // hidden by a saved filter); in that case we silently skip.
    {
        const int savedId =
            QSettings().value("playback/lastPlayedId", -1).toInt();
        if (savedId > 0)
        {
            const int row = m_playlistModel->rowForId(savedId);
            if (row >= 0)
            {
                const QModelIndex idx = m_playlistModel->index(row);
                m_playlistView->setCurrentIndex(idx);
                m_playlistView->scrollTo(idx,
                    QAbstractItemView::PositionAtCenter);
                // currentChanged → onPlaylistItemSelected fires from
                // setCurrentIndex above and populates m_currentItem +
                // rating/memo/thumbnail, so no extra work needed here.
            }
        }
    }

    m_fsHideTimer = new QTimer(this);
    m_fsHideTimer->setSingleShot(true);
    m_fsHideTimer->setInterval(700);
    connect(m_fsHideTimer, &QTimer::timeout, this, [this] {
        if (isFullScreen()) m_controlsBar->hide();
    });
}

// ============================================================
// Database initialisation
// ============================================================

void MainWindow::setupDatabase()
{
    // Check the SQL plugin is loadable before attempting to open a connection
    if (!QSqlDatabase::isDriverAvailable("QSQLITE"))
    {
        const QString msg =
            "QSQLITE driver not found.\n\n"
            "Make sure sqldrivers\\qsqlite.dll exists next to the executable.\n"
            "The playlist will not work until this is resolved.";

        qWarning() << "[DB] QSQLITE driver unavailable";
        QTimer::singleShot(0, this, [this, msg]()
        {
            QMessageBox::critical(this, "Database Error", msg);
        });
        statusBar()->showMessage("DB: QSQLITE 드라이버 없음", 0);
        return;
    }

    const QString appData = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);

    if (!QDir().mkpath(appData))
        qWarning() << "[DB] mkpath failed for:" << appData;

    const QString dbPath = appData + "/videoplayer.db";
    qDebug() << "[DB] opening:" << dbPath;

    if (!m_db.initialize(dbPath))
    {
        const QString logPath = appData + "/app.log";

        // Defer the dialog until after the event loop starts so the main window
        // is fully visible when the error pops up.
        QTimer::singleShot(0, this, [this, dbPath, logPath]()
        {
            QMessageBox::critical(this, "Database Error",
                "SQLite 데이터베이스를 열지 못했습니다.\n\n"
                "경로: " + dbPath + "\n\n"
                "로그 파일을 확인하세요:\n" + logPath);
        });

        statusBar()->showMessage("DB 초기화 실패 — 로그 파일 확인 필요", 0);
        return;
    }

    qDebug() << "[DB] initialised OK:" << dbPath;
    statusBar()->showMessage("DB: " + dbPath, 5000);
}

// ============================================================
// UI construction
// ============================================================

void MainWindow::setupUI()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    m_leftPanel = buildLeftPanel();
    splitter->addWidget(m_leftPanel);
    splitter->addWidget(buildRightPanel());
    splitter->setStretchFactor(0, 1);   // left  panel: 1 part
    splitter->setStretchFactor(1, 3);   // right panel: 3 parts
    splitter->setSizes({300, 900});
    splitter->setChildrenCollapsible(false);

    root->addWidget(splitter);
}

// ----------------------------------------
// Left panel: search + playlist + meta
// ----------------------------------------
// Small helper: a flat, round, icon-only button used in the toolbars.
static QPushButton* makeIconButton(char16_t glyph, const QString& tip,
                                   int diameter = 36, int iconPx = 20,
                                   const QColor& color = QColor(0xcf, 0xd3, 0xda))
{
    auto* btn = new QPushButton;
    btn->setIcon(Icons::icon(glyph, color, iconPx));
    btn->setIconSize(QSize(iconPx, iconPx));
    btn->setFixedSize(diameter, diameter);
    btn->setProperty("iconButton", true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(tip);
    return btn;
}

QWidget* MainWindow::buildLeftPanel()
{
    auto* panel  = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    // ---- Search + Add / Remove ----
    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(6);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("제목 검색…");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->addAction(Icons::icon(Icons::Search, QColor(0x9a, 0xa0, 0xa8), 18),
                            QLineEdit::LeadingPosition);
    searchRow->addWidget(m_searchEdit);

    auto* addBtn = makeIconButton(Icons::Add, "영상 파일 추가");
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::onAddFiles);
    searchRow->addWidget(addBtn);

    auto* addFolderBtn = makeIconButton(Icons::FolderOpen, "폴더 추가 (하위까지 스캔)");
    connect(addFolderBtn, &QPushButton::clicked, this, &MainWindow::onAddFolder);
    searchRow->addWidget(addFolderBtn);

    auto* removeBtn = makeIconButton(Icons::Delete, "선택 항목 제거",
                                     36, 20, QColor(0xe0, 0x8a, 0x8a));
    connect(removeBtn, &QPushButton::clicked, this, &MainWindow::onRemoveSelected);
    searchRow->addWidget(removeBtn);

    layout->addLayout(searchRow);

    // ---- Rating filter row ----
    {
        auto* filterRow = new QHBoxLayout;
        filterRow->setSpacing(6);

        auto* starLabel = new QLabel("최소 평점");
        starLabel->setToolTip("이 점수 이상인 영상만 목록에 표시합니다");
        filterRow->addWidget(starLabel);

        m_minRatingSpinBox = new QSpinBox;
        m_minRatingSpinBox->setRange(0, 100);
        m_minRatingSpinBox->setSingleStep(5);
        m_minRatingSpinBox->setValue(0);
        m_minRatingSpinBox->setSpecialValueText("전체");   // 0 → "전체" 표시
        m_minRatingSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_minRatingSpinBox->setAlignment(Qt::AlignCenter);
        m_minRatingSpinBox->setFixedWidth(70);
        m_minRatingSpinBox->setToolTip("스크롤 또는 입력 · 0 = 전체 표시");
        filterRow->addWidget(m_minRatingSpinBox);

        auto* resetBtn = new QPushButton("초기화");
        resetBtn->setToolTip("점수 필터 해제");
        connect(resetBtn, &QPushButton::clicked, this, [this] {
            m_minRatingSpinBox->setValue(0);
        });
        filterRow->addWidget(resetBtn);
        filterRow->addStretch();

        layout->addLayout(filterRow);
    }

    // ---- Playlist ----
    m_playlistModel = new PlaylistModel(this);

    m_playlistView = new QListView;
    m_playlistView->setModel(m_playlistModel);
    // ExtendedSelection: Ctrl+click toggles, Shift+click range-selects.
    // Enables bulk remove from the playlist.
    m_playlistView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_playlistView->setAlternatingRowColors(true);
    m_playlistView->setMinimumHeight(120);
    m_playlistView->setAcceptDrops(true);
    m_playlistView->setDropIndicatorShown(true);
    m_playlistView->setUniformItemSizes(true);
    m_playlistView->installEventFilter(this);
    layout->addWidget(m_playlistView, 1);   // stretch factor 1

    // ---- Index-sheet (thumbnail) preview ----
    auto* thumbGroup  = new QGroupBox("인덱스 시트");
    auto* thumbLayout = new QVBoxLayout(thumbGroup);
    thumbLayout->setContentsMargins(10, 8, 10, 10);
    thumbLayout->setSpacing(8);

    m_thumbnailLabel = new ThumbnailLabel("미리보기 없음");
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);
    m_thumbnailLabel->setMinimumHeight(240);
    m_thumbnailLabel->setScaledContents(false);
    // Don't let the pixmap lock the label to its native size — we rescale
    // manually on every resizeEvent and want the label to be free to
    // shrink below the source image.
    m_thumbnailLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_thumbnailLabel->setStyleSheet(
        "QLabel { background:#1a1b1e; color:#6b7079; "
        "         border:1px dashed #3a3d44; border-radius:6px; font-size:12px; }");
    m_thumbnailLabel->setToolTip("더블클릭하여 크게 보기");
    thumbLayout->addWidget(m_thumbnailLabel);

    // ---- Thumbnail action buttons ----
    auto* thumbBtnRow = new QHBoxLayout;
    thumbBtnRow->setSpacing(6);

    m_importThumbButton = new QPushButton("  이미지 가져오기");
    m_importThumbButton->setIcon(Icons::icon(Icons::Image, QColor(0xcf, 0xd3, 0xda), 18));
    m_importThumbButton->setToolTip("JPG/PNG 파일을 썸네일로 지정");
    thumbBtnRow->addWidget(m_importThumbButton);

    m_autoThumbButton = new QPushButton("  자동 생성");
    m_autoThumbButton->setIcon(Icons::icon(Icons::AutoFixHigh, QColor(0xcf, 0xd3, 0xda), 18));
    m_autoThumbButton->setToolTip("영상 10% 지점에서 썸네일 자동 캡처");
    thumbBtnRow->addWidget(m_autoThumbButton);

    thumbLayout->addLayout(thumbBtnRow);
    layout->addWidget(thumbGroup, 2);   // 썸네일이 플레이리스트보다 더 큰 비율

    // ---- Rating & Memo ----
    auto* metaGroup  = new QGroupBox("평점 & 메모");
    auto* metaLayout = new QVBoxLayout(metaGroup);
    metaLayout->setContentsMargins(10, 8, 10, 10);
    metaLayout->setSpacing(8);

    auto* ratingRow = new QHBoxLayout;
    ratingRow->setSpacing(6);
    ratingRow->addWidget(new QLabel("평점"));
    m_ratingSpinBox = new QSpinBox;
    m_ratingSpinBox->setRange(0, 100);
    m_ratingSpinBox->setSingleStep(5);
    m_ratingSpinBox->setValue(0);
    m_ratingSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_ratingSpinBox->setAlignment(Qt::AlignCenter);
    m_ratingSpinBox->setFixedWidth(64);
    m_ratingSpinBox->setToolTip("0 – 100 · 스크롤 또는 입력");
    ratingRow->addWidget(m_ratingSpinBox);
    ratingRow->addWidget(new QLabel("/ 100"));
    ratingRow->addStretch();
    metaLayout->addLayout(ratingRow);

    m_memoEdit = new QTextEdit;
    m_memoEdit->setMaximumHeight(90);
    m_memoEdit->setPlaceholderText("메모 / 감상…");
    metaLayout->addWidget(m_memoEdit);

    m_saveButton = new QPushButton("  저장");
    m_saveButton->setObjectName("AccentButton");
    m_saveButton->setIcon(Icons::icon(Icons::Save, QColor(0xff, 0xff, 0xff), 18));
    m_saveButton->setToolTip("평점과 메모 저장");
    metaLayout->addWidget(m_saveButton);

    layout->addWidget(metaGroup);

    return panel;
}

// ----------------------------------------
// Right panel: video surface + controls
// ----------------------------------------
QWidget* MainWindow::buildRightPanel()
{
    m_rightPanel   = new QWidget;
    auto* panel    = m_rightPanel;
    auto* layout   = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_mpvWidget = new MpvPlayerWidget(panel);
    m_mpvWidget->setMinimumSize(640, 360);
    m_mpvWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_mpvWidget, 1);

    m_controlsBar = buildControlsBar();
    layout->addWidget(m_controlsBar);

    return panel;
}

// ----------------------------------------
// Controls bar (seek, transport, volume, upscale)
// ----------------------------------------
QWidget* MainWindow::buildControlsBar()
{
    const QColor kIcon(0xcf, 0xd3, 0xda);   // normal icon colour

    auto* bar = new QFrame;
    bar->setObjectName("ControlsBar");
    bar->setFixedHeight(96);

    auto* vl = new QVBoxLayout(bar);
    vl->setContentsMargins(16, 8, 16, 10);
    vl->setSpacing(8);

    // ---- Seek row: elapsed/total + slider ----
    auto* seekRow = new QHBoxLayout;
    seekRow->setSpacing(10);

    m_timeLabel = new QLabel("00:00 / 00:00");
    m_timeLabel->setStyleSheet("color:#9aa0a8; font-family:'Consolas','Cascadia Mono',monospace;");
    m_timeLabel->setMinimumWidth(120);
    m_timeLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    seekRow->addWidget(m_timeLabel);

    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, 0);
    m_seekSlider->setSingleStep(5);
    m_seekSlider->setPageStep(30);
    m_seekSlider->setTracking(false);   // don't seek on every pixel move
    // Don't steal arrow keys — our QAction shortcuts handle relative seek.
    m_seekSlider->setFocusPolicy(Qt::NoFocus);
    // Click-on-groove = jump-to-here (absolute), not pageStep nudge.
    {
        auto* proxy = new AbsoluteSliderStyle(m_seekSlider->style());
        proxy->setParent(m_seekSlider);
        m_seekSlider->setStyle(proxy);
    }
    seekRow->addWidget(m_seekSlider, 1);

    vl->addLayout(seekRow);

    // ---- Transport row: [volume] --- [prev play stop next] --- [extras] ----
    auto* row = new QHBoxLayout;
    row->setSpacing(8);

    // Left group: mute toggle + volume slider
    m_volumeButton = makeIconButton(Icons::VolumeUp, "음소거", 32, 18, kIcon);
    m_volumeButton->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_volumeButton);

    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(m_lastVolume);
    m_volumeSlider->setFixedWidth(120);
    m_volumeSlider->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_volumeSlider);

    row->addStretch();

    // Center group: transport. Play is the primary, oversized button.
    m_prevButton      = makeIconButton(Icons::SkipPrevious, "이전", 40, 24, kIcon);
    m_playPauseButton = makeIconButton(Icons::PlayArrow,    "재생/일시정지", 46, 28,
                                       QColor(0xff, 0xff, 0xff));
    m_playPauseButton->setObjectName("PrimaryButton");
    m_stopButton      = makeIconButton(Icons::Stop,         "정지", 40, 24, kIcon);
    m_nextButton      = makeIconButton(Icons::SkipNext,     "다음", 40, 24, kIcon);

    for (auto* btn : { m_prevButton, m_playPauseButton, m_stopButton, m_nextButton })
    {
        btn->setFocusPolicy(Qt::NoFocus);
        row->addWidget(btn);
    }

    row->addStretch();

    // Right group: upscale, repeat, screenshot
    // Item order MUST match MpvPlayerWidget::UpscaleMode (Off=0,
    // Standard=1, NvidiaNis=2) — currentIndex() is used as the enum value.
    m_upscaleCombo = new QComboBox;
    m_upscaleCombo->addItem("끄기");          // Off
    m_upscaleCombo->addItem("표준");          // Standard
    m_upscaleCombo->addItem("NVIDIA NIS");    // NvidiaNis
    m_upscaleCombo->setFixedWidth(118);
    m_upscaleCombo->setToolTip(
        "업스케일 모드\n"
        "끄기: 빠름 (bilinear)\n"
        "표준: Lanczos + 가벼운 샤프닝 (모든 GPU)\n"
        "NVIDIA NIS: GPU 셰이더 적응형 업스케일 + 샤프닝");
    row->addWidget(m_upscaleCombo);

    m_repeatButton = makeIconButton(Icons::Repeat, "반복: 없음 → 1편 → 전체", 40, 22, kIcon);
    m_repeatButton->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_repeatButton);

    m_screenshotButton = makeIconButton(Icons::PhotoCamera,
                                        "현재 프레임을 썸네일로 저장", 40, 22, kIcon);
    m_screenshotButton->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_screenshotButton);

    vl->addLayout(row);

    return bar;
}

// ============================================================
// Signal / slot wiring
// ============================================================

void MainWindow::setupConnections()
{
    // ---- Playlist management ----
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &MainWindow::onSearchTextChanged);

    connect(m_minRatingSpinBox, &QSpinBox::valueChanged,
            this, [this](int value) {
                m_playlistModel->setMinRating(value);
                QSettings().setValue("filter/minRating", value);
            });

    connect(m_playlistView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this, &MainWindow::onPlaylistItemSelected);

    connect(m_playlistView, &QListView::doubleClicked,
            this, &MainWindow::onPlaylistItemDoubleClicked);

    // ---- Metadata ----
    connect(m_saveButton, &QPushButton::clicked,
            this, &MainWindow::onSaveMetadata);

    // ---- Transport ----
    connect(m_prevButton,      &QPushButton::clicked,
            this, &MainWindow::onPlayPrevious);
    connect(m_playPauseButton, &QPushButton::clicked,
            this, &MainWindow::onPlayPauseRequested);
    connect(m_stopButton,      &QPushButton::clicked,
            m_mpvWidget, &MpvPlayerWidget::stop);
    connect(m_nextButton,      &QPushButton::clicked,
            this, &MainWindow::onPlayNext);

    // ---- Seek slider ----
    connect(m_seekSlider, &QSlider::sliderPressed,
            this, &MainWindow::onSeekSliderPressed);
    connect(m_seekSlider, &QSlider::sliderReleased,
            this, &MainWindow::onSeekSliderReleased);

    // ---- Volume ----
    connect(m_volumeSlider, &QSlider::valueChanged,
            m_mpvWidget, &MpvPlayerWidget::setVolume);

    // Reflect the level on the mute-button icon (off / down / up).
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int v) {
        const QColor c(0xcf, 0xd3, 0xda);
        const char16_t g = (v == 0)  ? Icons::VolumeOff
                          : (v < 50)  ? Icons::VolumeDown
                                      : Icons::VolumeUp;
        m_volumeButton->setIcon(Icons::icon(g, c, 18));
        if (v > 0)
            m_lastVolume = v;   // remember for un-mute
    });

    // Mute toggle: 0 ↔ last non-zero level.
    connect(m_volumeButton, &QPushButton::clicked, this, [this] {
        if (m_volumeSlider->value() > 0)
            m_volumeSlider->setValue(0);
        else
            m_volumeSlider->setValue(m_lastVolume > 0 ? m_lastVolume : 80);
    });

    // ---- Upscaling ----
    // Combo index matches MpvPlayerWidget::UpscaleMode (Off=0,
    // Standard=1, NvidiaNis=2) by construction — see the item-add
    // block in setupUI. Persist the chosen mode on every change so
    // the next launch restores it.
    connect(m_upscaleCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                const auto mode = static_cast<MpvPlayerWidget::UpscaleMode>(index);
                m_mpvWidget->setUpscaleMode(mode);
                QSettings().setValue("playback/upscaleMode", index);
            });

    // ---- Repeat ----
    connect(m_repeatButton, &QPushButton::clicked,
            this, &MainWindow::onRepeatClicked);
    updateRepeatButton();   // initialise label

    // ---- Thumbnail ----
    connect(m_screenshotButton,    &QPushButton::clicked,
            this, &MainWindow::onTakeScreenshot);
    connect(m_importThumbButton,   &QPushButton::clicked,
            this, &MainWindow::onImportThumbnail);
    connect(m_autoThumbButton,     &QPushButton::clicked,
            this, &MainWindow::onAutoThumbnail);
    // 썸네일 영역 더블클릭으로도 이미지 가져오기
    m_thumbnailLabel->installEventFilter(this);

    // ---- MPV events → UI ----
    connect(m_mpvWidget, &MpvPlayerWidget::positionChanged,
            this, &MainWindow::onPositionChanged);
    connect(m_mpvWidget, &MpvPlayerWidget::durationChanged,
            this, &MainWindow::onDurationChanged);
    connect(m_mpvWidget, &MpvPlayerWidget::pauseStateChanged,
            this, &MainWindow::onPauseStateChanged);
    connect(m_mpvWidget, &MpvPlayerWidget::fileEnded,
            this, &MainWindow::onFileEnded);

    // Apply initial volume to mpv once it's running
    m_mpvWidget->setVolume(m_volumeSlider->value());
}

// ============================================================
// Slots — playlist management
// ============================================================

void MainWindow::onAddFiles()
{
    // Remember the last directory the user opened.  Falls back to the
    // system Movies folder the very first time.
    QSettings s;
    const QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString startDir =
        s.value("dialog/lastAddDir", defaultDir).toString();

    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Select Video Files",
        startDir,
        videoFileDialogFilter()
    );

    if (files.isEmpty())
        return;

    // Persist the folder of the first selected file so the next open
    // dialog lands in the same spot.
    s.setValue("dialog/lastAddDir", QFileInfo(files.first()).absolutePath());

    // Guard: DB must be ready before we do anything
    if (!m_db.isInitialized())
    {
        QMessageBox::critical(this, "Database Error",
            "The database is not initialised — cannot add files.\n\n"
            "Check the log file for details:\n" +
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            "/app.log");
        return;
    }

    qDebug() << "[Playlist] Adding" << files.size() << "file(s)";

    int added   = 0;
    int skipped = 0;   // already in DB (UNIQUE constraint)
    int paired  = 0;   // new entries that got a sibling "_index.*" sheet

    for (const QString& file : files)
    {
        // Auto-detect "<basename>_index.jpg|png|..." living next to the
        // video and attach it as the thumbnail on first insert.
        const QString indexSheet = findIndexSheetFor(file);

        const bool inserted = m_db.addMediaFile(file, indexSheet);
        if (inserted)
        {
            ++added;
            if (!indexSheet.isEmpty())
            {
                ++paired;
                qDebug() << "[Playlist] auto-paired index sheet:" << indexSheet;
            }
        }
        else
        {
            ++skipped;
        }
    }

    refreshPlaylist();

    const int total = m_playlistModel->rowCount();
    statusBar()->showMessage(
        QString("Added %1 new (index: %2)  |  %3 duplicate  |  total: %4")
            .arg(added).arg(paired).arg(skipped).arg(total),
        8000);
}

// ------------------------------------------------------------
// Recursively scan a folder for videos and add every new one to the
// playlist.  Pairs each video with its "<basename>_index.*" sibling
// when present (via findIndexSheetFor), matching the manual-add
// behaviour.
//
// A wait cursor is shown during the scan — networked folders (NAS,
// mapped drives) can be slow to traverse.
// ------------------------------------------------------------
void MainWindow::onAddFolder()
{
    if (!m_db.isInitialized())
    {
        QMessageBox::critical(this, "Database Error",
            "The database is not initialised — cannot add files.\n\n"
            "Check the log file for details:\n" +
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            "/app.log");
        return;
    }

    QSettings s;
    const QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString startDir =
        s.value("dialog/lastFolderAddDir", defaultDir).toString();

    const QString folder = QFileDialog::getExistingDirectory(
        this,
        "폴더 선택 (하위 디렉토리까지 재귀 스캔)",
        startDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (folder.isEmpty())
        return;

    s.setValue("dialog/lastFolderAddDir", folder);

    qDebug() << "[Playlist] Scanning folder recursively:" << folder;

    // NAS / mapped-drive scans can take a while — show a wait cursor
    // and guarantee restoration even if anything below throws.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    struct CursorGuard
    {
        ~CursorGuard() { QApplication::restoreOverrideCursor(); }
    } cursorGuard;

    statusBar()->showMessage(
        QString("폴더 스캔 중: %1").arg(folder), 0);

    int scanned = 0;
    int added   = 0;
    int skipped = 0;
    int paired  = 0;

    // QDir::Files alone would miss hidden files, but QDir::NoDotAndDotDot
    // is implicit for Files.  We leave hidden files out intentionally.
    QDirIterator it(folder,
                    videoNameFilters(),
                    QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories
                      | QDirIterator::FollowSymlinks);

    while (it.hasNext())
    {
        const QString path = it.next();
        ++scanned;

        const QString indexSheet = findIndexSheetFor(path);

        if (m_db.addMediaFile(path, indexSheet))
        {
            ++added;
            if (!indexSheet.isEmpty())
                ++paired;
        }
        else
        {
            ++skipped;
        }

        // Pump the event loop occasionally so the wait cursor / status
        // bar stay responsive during big scans.
        if ((scanned % 50) == 0)
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    refreshPlaylist();

    const int total = m_playlistModel->rowCount();
    qDebug().nospace()
        << "[Playlist] folder scan done — scanned=" << scanned
        << " added=" << added << " paired=" << paired
        << " duplicate=" << skipped;

    statusBar()->showMessage(
        QString("폴더 스캔 완료: %1개 검색  |  %2개 추가 (인덱스: %3)  |  %4개 중복  |  전체: %5")
            .arg(scanned).arg(added).arg(paired).arg(skipped).arg(total),
        10000);
}

void MainWindow::onRemoveSelected()
{
    // Collect IDs up-front — row indices become stale the instant we
    // start mutating the model, so we must resolve everything to stable
    // primary keys before touching the DB.
    const QModelIndexList rows =
        m_playlistView->selectionModel()->selectedRows();

    if (rows.isEmpty())
        return;

    QList<int> idsToRemove;
    idsToRemove.reserve(rows.size());
    for (const QModelIndex& idx : rows)
    {
        auto item = m_playlistModel->itemAt(idx.row());
        if (item.has_value())
            idsToRemove.append(item->id);
    }

    if (idsToRemove.isEmpty())
        return;

    // If the currently-playing item is about to disappear, stop mpv
    // first so it doesn't keep a file handle on a now-deleted entry.
    if (m_currentItem.has_value()
        && idsToRemove.contains(m_currentItem->id))
    {
        m_mpvWidget->stop();
        m_currentItem.reset();
    }

    int removed = 0;
    for (int id : idsToRemove)
    {
        if (m_db.removeMediaFile(id))
            ++removed;
    }

    refreshPlaylist();

    statusBar()->showMessage(
        QString("선택한 %1개 항목을 삭제했습니다").arg(removed), 4000);
}

void MainWindow::onSearchTextChanged(const QString& text)
{
    m_playlistModel->setFilter(text);

    // Persist so the same filter is in place after a restart — users
    // complained that the search box being cleared on every launch
    // forced them to re-type the filter they had been using.
    QSettings().setValue("filter/searchText", text);
}

void MainWindow::onPlaylistItemSelected(const QModelIndex& current,
                                        const QModelIndex& /*previous*/)
{
    if (!current.isValid())
        return;

    auto optItem = m_playlistModel->itemAt(current.row());
    if (!optItem.has_value())
        return;

    m_currentItem = optItem;
    loadCurrentItem(*m_currentItem);
}

void MainWindow::onPlaylistItemDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid())
        return;

    // Delegate to playItemAtRow so the "start playback" path is single-
    // sourced (last-played persistence, pause-reset, etc. all live there).
    playItemAtRow(index.row());
}

// ============================================================
// Slots — metadata
// ============================================================

void MainWindow::onSaveMetadata()
{
    if (!m_currentItem.has_value())
    {
        QMessageBox::information(this, "No Selection",
            "Select a video from the playlist first.");
        return;
    }

    const int     rating = m_ratingSpinBox->value();
    const QString memo   = m_memoEdit->toPlainText();

    (void)m_db.updateRating(m_currentItem->id, rating);
    (void)m_db.updateMemo(m_currentItem->id, memo);

    m_currentItem->rating = rating;
    m_currentItem->memo   = memo;

    m_playlistModel->updateItem(*m_currentItem);
}

// ============================================================
// QAction construction
//
//   Every QAction with a customisable shortcut is:
//     - given a stable objectName() used as its QSettings key
//     - given a default QKeySequence
//     - set to Qt::WindowShortcut — fires anywhere inside the main
//       window, but NOT when a modal dialog (Options, FileDialog, …)
//       is active, which is exactly what we want
//     - added to MainWindow via addAction() so shortcuts fire even
//       when the action isn't in any menu yet
//     - appended to m_shortcutActions for the Options dialog to edit
// ============================================================

void MainWindow::createActions()
{
    // Small helper to keep action creation uniform and tidy.
    auto make = [this](const QString&      id,
                       const QString&      text,
                       const QKeySequence& defSc,
                       auto                slot,
                       bool                customisable = true) -> QAction*
    {
        auto* a = new QAction(text, this);
        a->setObjectName(id);
        a->setShortcut(defSc);
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);                         // register on window
        if (customisable)
            m_shortcutActions.append(a);
        return a;
    };

    // ---- Playback ----
    m_actPlayPause = make(
        "playPause", "재생 / 일시정지",
        QKeySequence(Qt::Key_Space),
        [this]() { onPlayPauseRequested(); });

    m_actStop = make(
        "stop", "정지",
        QKeySequence(Qt::Key_MediaStop),
        [this]() { m_mpvWidget->stop(); });

    m_actSeekBack = make(
        "seekBack", "뒤로 이동 (5초)",
        QKeySequence(Qt::Key_Left),
        &MainWindow::onSeekBack);

    m_actSeekForward = make(
        "seekForward", "앞으로 이동 (5초)",
        QKeySequence(Qt::Key_Right),
        &MainWindow::onSeekForward);

    m_actPrevious = make(
        "previous", "이전 트랙",
        QKeySequence(Qt::CTRL | Qt::Key_Left),
        &MainWindow::onPlayPrevious);

    m_actNext = make(
        "next", "다음 트랙",
        QKeySequence(Qt::CTRL | Qt::Key_Right),
        &MainWindow::onPlayNext);

    m_actToggleRepeat = make(
        "toggleRepeat", "반복 모드 전환",
        QKeySequence(Qt::Key_R),
        &MainWindow::onRepeatClicked);

    m_actToggleFullscreen = make(
        "toggleFullscreen", "전체화면 전환",
        QKeySequence(Qt::Key_Return),
        &MainWindow::onToggleFullscreen);

    // Esc acts as "exit only" — no effect when we're already windowed,
    // so it doesn't interfere with anything else.
    m_actExitFullscreen = make(
        "exitFullscreen", "전체화면 종료",
        QKeySequence(Qt::Key_Escape),
        &MainWindow::onExitFullscreen);

    // ---- File / playlist ----
    m_actAddFiles = make(
        "addFiles", "파일 추가…",
        QKeySequence::Open,                    // Ctrl+O
        &MainWindow::onAddFiles);

    m_actAddFolder = make(
        "addFolder", "폴더 추가 (재귀)…",
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
        &MainWindow::onAddFolder);

    m_actRemoveSelected = make(
        "removeSelected", "선택 항목 삭제",
        QKeySequence(Qt::Key_Delete),
        &MainWindow::onRemoveSelected);

    // ---- Metadata / tools ----
    m_actSaveMeta = make(
        "saveMeta", "평가 / 메모 저장",
        QKeySequence::Save,                    // Ctrl+S
        &MainWindow::onSaveMetadata);

    m_actScreenshot = make(
        "screenshot", "썸네일 캡처",
        QKeySequence(Qt::Key_F9),
        &MainWindow::onTakeScreenshot);

    // ---- Application / dialogs ----
    m_actOptions = make(
        "options", "옵션…",
        QKeySequence(Qt::CTRL | Qt::Key_Comma),
        &MainWindow::onOptionsTriggered);

    // Quit intentionally non-customisable — standard platform shortcut.
    m_actQuit = make(
        "quit", "종료",
        QKeySequence::Quit,                    // Ctrl+Q
        [this]() { close(); },
        /*customisable*/ false);
}

// ============================================================
// Menu bar
// ============================================================

void MainWindow::setupMenuBar()
{
    auto* mb = menuBar();

    // --- 파일 ---
    auto* fileMenu = mb->addMenu("파일(&F)");
    fileMenu->addAction(m_actAddFiles);
    fileMenu->addAction(m_actAddFolder);
    fileMenu->addAction(m_actRemoveSelected);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actQuit);

    // --- 재생 ---
    auto* playMenu = mb->addMenu("재생(&P)");
    playMenu->addAction(m_actPlayPause);
    playMenu->addAction(m_actStop);
    playMenu->addSeparator();
    playMenu->addAction(m_actSeekBack);
    playMenu->addAction(m_actSeekForward);
    playMenu->addSeparator();
    playMenu->addAction(m_actPrevious);
    playMenu->addAction(m_actNext);
    playMenu->addSeparator();
    playMenu->addAction(m_actToggleRepeat);
    playMenu->addAction(m_actToggleFullscreen);

    // --- 도구 ---
    auto* toolsMenu = mb->addMenu("도구(&T)");
    toolsMenu->addAction(m_actSaveMeta);
    toolsMenu->addAction(m_actScreenshot);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_actOptions);
}

// ============================================================
// Shortcut persistence  (QSettings — uses CustomMedia / VideoPlayer)
// ============================================================

void MainWindow::loadShortcuts()
{
    QSettings s;
    s.beginGroup("shortcuts");

    for (QAction* a : m_shortcutActions)
    {
        const QString id = a->objectName();
        if (id.isEmpty())
            continue;

        if (!s.contains(id))
            continue;

        const QString str = s.value(id).toString();
        a->setShortcut(QKeySequence(str));
    }

    s.endGroup();
}

void MainWindow::saveShortcuts()
{
    QSettings s;
    s.beginGroup("shortcuts");

    for (QAction* a : m_shortcutActions)
    {
        const QString id = a->objectName();
        if (id.isEmpty())
            continue;

        s.setValue(id, a->shortcut().toString(QKeySequence::PortableText));
    }

    s.endGroup();
}

// ============================================================
// Slot — relative seek (arrow-key shortcuts)
// ============================================================

void MainWindow::onSeekBack()
{
    if (m_mpvWidget && m_currentItem.has_value())
        m_mpvWidget->seekRelative(-5.0);
}

void MainWindow::onSeekForward()
{
    if (m_mpvWidget && m_currentItem.has_value())
        m_mpvWidget->seekRelative(+5.0);
}

// ============================================================
// Slot — options dialog
// ============================================================

void MainWindow::onOptionsTriggered()
{
    OptionsDialog dlg(m_shortcutActions, this);

    // Seed the dialog from the currently-active NIS sharpness so the
    // slider shows the user's last choice rather than the default.
    dlg.setNisSharpness(m_mpvWidget->nisSharpness());

    if (dlg.exec() == QDialog::Accepted)
    {
        // Dialog has already applied the new shortcuts to the QActions.
        // Persist them so they survive restart.
        saveShortcuts();

        // Pull general-tab values out of the dialog and apply +
        // persist. setNisSharpness re-applies the profile immediately
        // when NIS is the active mode; otherwise it just stores the
        // value for the next time NIS is selected.
        const double sharp = dlg.nisSharpness();
        m_mpvWidget->setNisSharpness(sharp);
        QSettings().setValue("playback/nisSharpness", sharp);

        statusBar()->showMessage("옵션이 저장되었습니다", 4000);
    }
}

// ============================================================
// Slot — video-only fullscreen toggle
//
//   "전체화면" here means the VIDEO fills the screen — not the whole
//   application window.  We achieve that by hiding every sibling of
//   m_mpvWidget (left panel, controls bar, menu bar, status bar) and
//   then calling showFullScreen() on the main window.  The right
//   panel's QVBoxLayout gives all remaining space to m_mpvWidget
//   (stretch 1 + Expanding size policy), so it fills the screen.
//
//   Exit: Enter (toggle) or Esc (exit-only).
// ============================================================

void MainWindow::onToggleFullscreen()
{
    if (isFullScreen())
    {
        onExitFullscreen();
        return;
    }

    m_savedGeometry = saveGeometry();

    if (m_leftPanel) m_leftPanel->setVisible(false);
    menuBar()->setVisible(false);
    statusBar()->setVisible(false);

    // Detach controls bar from the right-panel layout so it can float
    // as a transparent overlay on top of the video.
    if (m_controlsBar && m_rightPanel)
    {
        m_rightPanel->layout()->removeWidget(m_controlsBar);
        m_controlsBar->setParent(this);
        m_controlsBar->hide();
    }

    showFullScreen();

    // Position the floating bar now that the window has full-screen geometry.
    positionFsControlsBar();

    // Intercept all mouse-move events to implement auto-show/hide.
    qApp->installEventFilter(this);
}

void MainWindow::onExitFullscreen()
{
    if (!isFullScreen())
        return;

    qApp->removeEventFilter(this);
    m_fsHideTimer->stop();

    // Re-attach controls bar to the right panel layout.
    if (m_controlsBar && m_rightPanel)
    {
        auto* vbl = qobject_cast<QVBoxLayout*>(m_rightPanel->layout());
        if (vbl) vbl->addWidget(m_controlsBar);
        m_controlsBar->show();
    }

    showNormal();
    if (!m_savedGeometry.isEmpty())
        restoreGeometry(m_savedGeometry);

    if (m_leftPanel) m_leftPanel->setVisible(true);
    menuBar()->setVisible(true);
    statusBar()->setVisible(true);
}

// ============================================================
// Slot — repeat mode cycling
// ============================================================

void MainWindow::onRepeatClicked()
{
    switch (m_repeatMode)
    {
    case RepeatMode::None: m_repeatMode = RepeatMode::One; break;
    case RepeatMode::One:  m_repeatMode = RepeatMode::All; break;
    case RepeatMode::All:  m_repeatMode = RepeatMode::None; break;
    }
    updateRepeatButton();
}

void MainWindow::updateRepeatButton()
{
    const QColor off(0xcf, 0xd3, 0xda);
    const QColor on (0x4f, 0x93, 0xff);   // accent — active repeat

    switch (m_repeatMode)
    {
    case RepeatMode::None:
        m_repeatButton->setIcon(Icons::icon(Icons::Repeat, off, 22));
        m_repeatButton->setToolTip("반복: 없음 (클릭 → 1편)");
        break;
    case RepeatMode::One:
        m_repeatButton->setIcon(Icons::icon(Icons::RepeatOne, on, 22));
        m_repeatButton->setToolTip("반복: 1편 (클릭 → 전체)");
        break;
    case RepeatMode::All:
        m_repeatButton->setIcon(Icons::icon(Icons::Repeat, on, 22));
        m_repeatButton->setToolTip("반복: 전체 (클릭 → 없음)");
        break;
    }
}

// ============================================================
// Slots — playback navigation
// ============================================================

void MainWindow::onPlayPauseRequested()
{
    // If mpv has a file loaded, behave like a plain play/pause toggle.
    // If it doesn't, Space (or the play button) shouldn't be a no-op —
    // start playback of whatever is currently selected in the playlist.
    // This covers two real cases:
    //
    //   1. Right after app startup, when we restored the last-played
    //      entry's *selection* without auto-playing it. Without this
    //      branch the user has to double-click to start, even though
    //      the row is already highlighted.
    //
    //   2. After the Stop button — mpv unloads the file but the
    //      playlist selection is still pointing at the same row.
    //
    // We prefer rowForId(m_currentItem->id) over currentPlaylistRow()
    // because filters or sorting could have shifted indices between
    // selection and now.
    if (m_mpvWidget->hasFile())
    {
        m_mpvWidget->togglePause();
        return;
    }

    int row = -1;
    if (m_currentItem.has_value())
        row = m_playlistModel->rowForId(m_currentItem->id);
    if (row < 0)
        row = currentPlaylistRow();

    if (row >= 0)
        playItemAtRow(row);
}

void MainWindow::onPlayPrevious()
{
    const int row = currentPlayingRow();
    if (row > 0)
    {
        m_playlistView->setCurrentIndex(m_playlistModel->index(row - 1));
        playItemAtRow(row - 1);
    }
}

void MainWindow::onPlayNext()
{
    const int row  = currentPlayingRow();
    const int last = m_playlistModel->rowCount() - 1;
    if (last < 0)
        return;

    if (row < last)
    {
        m_playlistView->setCurrentIndex(m_playlistModel->index(row + 1));
        playItemAtRow(row + 1);
    }
    else if (m_repeatMode == RepeatMode::All)
    {
        // 전체 반복 — 마지막 곡에서 처음으로 돌아감
        m_playlistView->setCurrentIndex(m_playlistModel->index(0));
        playItemAtRow(0);
    }
}

// ============================================================
// Slots — MPV event handlers
// ============================================================

void MainWindow::onPositionChanged(double seconds)
{
    if (!m_userSeeking)
        m_seekSlider->setValue(static_cast<int>(seconds));

    m_timeLabel->setText(formatTime(seconds) + " / " + formatTime(m_duration));
}

void MainWindow::onDurationChanged(double seconds)
{
    m_duration = seconds;
    m_seekSlider->setRange(0, static_cast<int>(seconds));
    m_timeLabel->setText(formatTime(0) + " / " + formatTime(seconds));
}

void MainWindow::onPauseStateChanged(bool paused)
{
    m_playPauseButton->setIcon(
        Icons::icon(paused ? Icons::PlayArrow : Icons::Pause,
                    QColor(0xff, 0xff, 0xff), 28));
}

void MainWindow::onFileEnded()
{
    switch (m_repeatMode)
    {
    case RepeatMode::One:
        // 현재 파일 처음부터 다시 재생
        if (m_currentItem.has_value())
            m_mpvWidget->loadFile(m_currentItem->filePath);
        break;

    case RepeatMode::All:
        {
            const int row  = currentPlayingRow();
            const int last = m_playlistModel->rowCount() - 1;
            if (last < 0)
                break;
            const int next = (row < last) ? row + 1 : 0;   // 마지막이면 처음으로
            m_playlistView->setCurrentIndex(m_playlistModel->index(next));
            playItemAtRow(next);
        }
        break;

    case RepeatMode::None:
    default:
        onPlayNext();   // 다음 트랙 (마지막이면 정지)
        break;
    }
}

// ============================================================
// Slots — seek slider
// ============================================================

void MainWindow::onSeekSliderPressed()
{
    m_userSeeking = true;
}

void MainWindow::onSeekSliderReleased()
{
    m_userSeeking = false;
    m_mpvWidget->seek(static_cast<double>(m_seekSlider->value()));
}

// ============================================================
// Slot — thumbnail screenshot
// ============================================================

void MainWindow::onTakeScreenshot()
{
    if (!m_currentItem.has_value())
    {
        QMessageBox::information(this, "No Selection",
            "Select and play a video first.");
        return;
    }

    const QString thumbDir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + "/thumbnails";
    QDir().mkpath(thumbDir);

    const QString thumbPath = QString("%1/%2.jpg")
        .arg(thumbDir)
        .arg(m_currentItem->id);

    m_mpvWidget->takeScreenshot(thumbPath);

    // The screenshot command is asynchronous — wait briefly then update the DB
    QTimer::singleShot(600, this, [this, thumbPath]()
    {
        if (!m_currentItem.has_value())
            return;

        (void)m_db.updateThumbnail(m_currentItem->id, thumbPath);
        m_currentItem->thumbnailPath = thumbPath;
        m_playlistModel->updateItem(*m_currentItem);
        updateThumbnailDisplay(thumbPath);
    });
}

// ============================================================
// Slot — import existing image as thumbnail
// ============================================================

void MainWindow::onImportThumbnail()
{
    if (!m_currentItem.has_value())
    {
        QMessageBox::information(this, "선택 없음",
            "먼저 플레이리스트에서 영상을 선택하세요.");
        return;
    }

    // Remember the last directory the user picked a thumbnail from.
    QSettings s;
    const QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString startDir =
        s.value("dialog/lastThumbDir", defaultDir).toString();

    const QString src = QFileDialog::getOpenFileName(
        this,
        "썸네일 이미지 선택",
        startDir,
        "이미지 파일 (*.jpg *.jpeg *.png *.bmp *.webp);;모든 파일 (*)"
    );

    if (src.isEmpty())
        return;

    s.setValue("dialog/lastThumbDir", QFileInfo(src).absolutePath());

    // 앱 전용 thumbnails 폴더에 복사 (원본 경로 의존성 제거)
    const QString thumbDir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + "/thumbnails";
    QDir().mkpath(thumbDir);

    const QString ext      = QFileInfo(src).suffix();
    const QString destPath = QString("%1/%2.%3")
        .arg(thumbDir)
        .arg(m_currentItem->id)
        .arg(ext.isEmpty() ? "jpg" : ext);

    // 같은 경로면 복사 불필요
    if (src != destPath)
    {
        QFile::remove(destPath);
        if (!QFile::copy(src, destPath))
        {
            QMessageBox::warning(this, "복사 실패",
                "이미지 파일을 복사하지 못했습니다.\n원본 경로를 직접 사용합니다.");
            // 복사 실패 시 원본 경로 그대로 저장
            (void)m_db.updateThumbnail(m_currentItem->id, src);
            m_currentItem->thumbnailPath = src;
            m_playlistModel->updateItem(*m_currentItem);
            updateThumbnailDisplay(src);
            return;
        }
    }

    (void)m_db.updateThumbnail(m_currentItem->id, destPath);
    m_currentItem->thumbnailPath = destPath;
    m_playlistModel->updateItem(*m_currentItem);
    updateThumbnailDisplay(destPath);
}

// ============================================================
// Slot — auto-generate an index-sheet thumbnail
//
// Mirrors the Python reference (reference/index_sheet.py): 12
// evenly-spaced frames between 5 % and 95 % of the timeline,
// laid out as a 4 × 3 grid with a metadata header strip.  The
// heavy lifting lives in IndexSheetGenerator above; this slot
// just validates state, builds the output path, and kicks the
// generator off.
// ============================================================

void MainWindow::onAutoThumbnail()
{
    if (!m_currentItem.has_value())
    {
        QMessageBox::information(this, "선택 없음",
            "먼저 플레이리스트에서 영상을 선택하세요.");
        return;
    }

    const double dur = m_mpvWidget->duration();
    if (dur <= 0.0)
    {
        // No video loaded yet — load it and retry once the decoder
        // has reported a duration.  Same recovery as the previous
        // single-frame implementation.
        m_mpvWidget->loadFile(m_currentItem->filePath);
        QTimer::singleShot(1500, this, &MainWindow::onAutoThumbnail);
        return;
    }

    const QString thumbDir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + "/thumbnails";
    QDir().mkpath(thumbDir);

    const QString thumbPath = QString("%1/%2.jpg")
        .arg(thumbDir)
        .arg(m_currentItem->id);

    // Capture the id by value — the user can swap playlist
    // selection while the chain is running, and we want the
    // result to land on the video that was selected when the
    // user clicked Auto Thumbnail.
    const int currentId = m_currentItem->id;

    statusBar()->showMessage("인덱스 시트 생성 중…", 0);

    auto gen = std::make_shared<IndexSheetGenerator>(
        m_mpvWidget,
        m_currentItem->filePath,
        thumbPath,
        [this, currentId, thumbPath](bool ok, const QString& outPath)
        {
            statusBar()->clearMessage();

            if (!ok)
            {
                QMessageBox::warning(this, "생성 실패",
                    "인덱스 시트 생성에 실패했습니다.\n"
                    "로그 파일을 확인하세요.");
                return;
            }

            (void)m_db.updateThumbnail(currentId, outPath);

            // Refresh the UI only if the user is still on the same
            // playlist row.  If they navigated away, the DB update
            // is enough — the new sheet will appear next time they
            // select this video.
            if (m_currentItem.has_value() && m_currentItem->id == currentId)
            {
                m_currentItem->thumbnailPath = outPath;
                m_playlistModel->updateItem(*m_currentItem);
                updateThumbnailDisplay(outPath);
            }

            qDebug() << "[IndexSheet] saved" << outPath;
        });
    gen->start();
}

// ============================================================
// Event filter — double-click on thumbnail label = import image
// ============================================================

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    // Fullscreen controls auto-show/hide based on mouse proximity to bottom edge.
    if (isFullScreen() && event->type() == QEvent::MouseMove)
    {
        auto* me       = static_cast<QMouseEvent*>(event);
        QPoint localPt = mapFromGlobal(me->globalPosition().toPoint());

        // Trigger zone: bottom 120 px of the window.
        if (localPt.y() >= height() - 120)
        {
            m_fsHideTimer->stop();
            if (!m_controlsBar->isVisible())
            {
                positionFsControlsBar();
                m_controlsBar->show();
                m_controlsBar->raise();
            }
        }
        else
        {
            if (m_controlsBar->isVisible() && !m_fsHideTimer->isActive())
                m_fsHideTimer->start();
        }
        // Do not consume — let the event reach its target.
    }

    // ── Drag-and-drop onto the playlist view ─────────────────────────────
    if (obj == m_playlistView)
    {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)
        {
            auto* de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData()->hasUrls())
            {
                de->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::Drop)
        {
            auto* de = static_cast<QDropEvent*>(event);
            if (de->mimeData()->hasUrls())
            {
                de->acceptProposedAction();

                const QSet<QString> videoExts =
                    QSet<QString>(videoExtensions().begin(), videoExtensions().end());

                int added = 0;
                for (const QUrl& url : de->mimeData()->urls())
                {
                    if (!url.isLocalFile()) continue;
                    const QString path = url.toLocalFile();
                    const QFileInfo fi(path);

                    if (fi.isDir())
                    {
                        // Recursively add all video files in the folder.
                        QDirIterator it(path,
                                        videoNameFilters(),
                                        QDir::Files | QDir::Readable,
                                        QDirIterator::Subdirectories
                                          | QDirIterator::FollowSymlinks);
                        while (it.hasNext())
                        {
                            const QString f = it.next();
                            if (m_db.addMediaFile(f, findIndexSheetFor(f)))
                                ++added;
                        }
                    }
                    else if (videoExts.contains(fi.suffix().toLower()))
                    {
                        if (m_db.addMediaFile(path, findIndexSheetFor(path)))
                            ++added;
                    }
                }

                if (added > 0)
                {
                    refreshPlaylist();
                    statusBar()->showMessage(
                        QString("%1개 파일 추가됨").arg(added), 3000);
                }
                return true;
            }
        }
    }

    if (obj == m_thumbnailLabel && event->type() == QEvent::MouseButtonDblClick)
    {
        // Preview popup — only if there's actually an image attached.
        // "이미지 가져오기" lives on its dedicated button now.
        if (m_currentItem.has_value()
            && !m_currentItem->thumbnailPath.isEmpty())
        {
            const QPixmap px(m_currentItem->thumbnailPath);
            if (!px.isNull())
            {
                ImageViewerDialog dlg(px,
                                      m_currentItem->title,
                                      this);
                dlg.exec();
            }
        }
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}

// ============================================================
// Private helpers
// ============================================================

void MainWindow::loadCurrentItem(const MediaItem& item)
{
    // Populate rating / memo fields
    m_ratingSpinBox->setValue(item.rating);
    m_memoEdit->setPlainText(item.memo);

    // Show the index-sheet thumbnail
    updateThumbnailDisplay(item.thumbnailPath);
}

void MainWindow::updateThumbnailDisplay(const QString& path)
{
    if (!path.isEmpty())
    {
        const QPixmap px(path);
        if (!px.isNull())
        {
            // Hand the native-resolution pixmap to ThumbnailLabel —
            // it will rescale automatically on every resizeEvent.
            m_thumbnailLabel->setSourcePixmap(px);
            return;
        }
    }

    m_thumbnailLabel->clearSource();
    m_thumbnailLabel->setText("미리보기 없음");
}

void MainWindow::refreshPlaylist()
{
    m_playlistModel->loadFromDatabase(m_db);
}

void MainWindow::playItemAtRow(int row)
{
    auto optItem = m_playlistModel->itemAt(row);
    if (!optItem.has_value())
        return;

    m_currentItem = optItem;
    loadCurrentItem(*m_currentItem);
    m_mpvWidget->loadFile(m_currentItem->filePath);

    // Remember this as the "last played" entry so the next launch can
    // restore the same selection. We save the id (stable across DB
    // re-orders and filter changes) rather than the row index. Written
    // on every actual playback start — double-click, next/prev, repeat.
    QSettings().setValue("playback/lastPlayedId", m_currentItem->id);
}

// ------------------------------------------------------------
// Play a file handed to us from the outside world (command line).
//
// Windows passes the media path as argv[1] when the user double-clicks an
// associated file or picks us from "Open with". We register the file in
// the DB (idempotent — duplicate paths are rejected by the UNIQUE
// constraint) so it joins the playlist and gets next/prev navigation plus
// its "<basename>_index.*" thumbnail, then play it through the normal
// playlist path.
// ------------------------------------------------------------
void MainWindow::openExternalFile(const QString& path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (abs.isEmpty() || !QFileInfo::exists(abs))
    {
        qWarning() << "[OpenFile] file not found:" << path;
        return;
    }

    qDebug() << "[OpenFile] external open request:" << abs;

    if (m_db.isInitialized())
    {
        // Clear any persisted search / min-rating filter, otherwise the
        // freshly-added entry could be hidden and we'd never be able to
        // select it in the playlist. Setting the widgets drives the model
        // (and persistence) through the normal signal path.
        if (m_searchEdit && !m_searchEdit->text().isEmpty())
            m_searchEdit->clear();
        if (m_minRatingSpinBox && m_minRatingSpinBox->value() != 0)
            m_minRatingSpinBox->setValue(0);

        const QString indexSheet = findIndexSheetFor(abs);
        (void)m_db.addMediaFile(abs, indexSheet);   // INSERT OR IGNORE

        // Move the entry to the top of the list — for both a brand-new
        // insert (already "now") and a re-watch of an existing file.
        if (const auto item = m_db.getMediaByPath(abs))
        {
            (void)m_db.bumpToFront(item->id);
            refreshPlaylist();

            const int row = m_playlistModel->rowForId(item->id);
            if (row >= 0)
            {
                const QModelIndex idx = m_playlistModel->index(row);
                m_playlistView->setCurrentIndex(idx);
                m_playlistView->scrollTo(idx,
                    QAbstractItemView::PositionAtTop);
                playItemAtRow(row);
                return;
            }
        }
    }

    // Fallback: DB unavailable, or the entry somehow couldn't be located.
    // Play the file directly so the user still sees their video.
    m_currentItem.reset();
    m_mpvWidget->loadFile(abs);
}

int MainWindow::currentPlaylistRow() const
{
    const QModelIndex idx = m_playlistView->currentIndex();
    return idx.isValid() ? idx.row() : -1;
}

int MainWindow::currentPlayingRow() const
{
    // Prefer the row of the item actually playing — its id is stable even
    // if the user has since highlighted a different row in the list.
    if (m_currentItem.has_value())
    {
        const int row = m_playlistModel->rowForId(m_currentItem->id);
        if (row >= 0)
            return row;
    }
    return currentPlaylistRow();
}

QString MainWindow::formatTime(double seconds) const
{
    const int total = static_cast<int>(seconds);
    const int h     = total / 3600;
    const int m     = (total % 3600) / 60;
    const int s     = total % 60;

    if (h > 0)
    {
        return QString("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    }

    return QString("%1:%2")
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

// ============================================================
// Fullscreen controls-bar overlay helpers
// ============================================================

void MainWindow::positionFsControlsBar()
{
    if (!m_controlsBar) return;
    int h = m_controlsBar->sizeHint().height();
    m_controlsBar->setGeometry(0, height() - h - 20, width(), h);
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);
    if (isFullScreen())
        positionFsControlsBar();
}
