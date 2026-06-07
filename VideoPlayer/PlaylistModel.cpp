#include "PlaylistModel.h"

#include <QDebug>
#include <QPainter>
#include <QFileInfo>
#include <algorithm>
#include <ranges>

namespace
{
// Grid thumbnail size — matches MainWindow's QListView iconSize so the
// pixmap is rendered 1:1 without further scaling by the view.
constexpr int kThumbW = 160;
constexpr int kThumbH = 90;
}

// ============================================================
// Constructor
// ============================================================

PlaylistModel::PlaylistModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

// ============================================================
// QAbstractListModel interface
// ============================================================

int PlaylistModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;

    return static_cast<int>(m_filteredItems.size());
}

QVariant PlaylistModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};

    const MediaItem& item = m_filteredItems[static_cast<std::size_t>(index.row())];

    switch (role)
    {
    case Qt::DisplayRole:
        return item.title;

    case Qt::DecorationRole:
        // Used by the grid (IconMode) view. In list mode the icon is
        // simply not painted (no iconSize set), so returning it always
        // is harmless and keeps both modes backed by the same model.
        return thumbnailFor(item);

    case Qt::ToolTipRole:
        return item.filePath;

    case static_cast<int>(MediaRole::Id):
        return item.id;

    case static_cast<int>(MediaRole::FilePath):
        return item.filePath;

    case static_cast<int>(MediaRole::ThumbnailPath):
        return item.thumbnailPath;

    case static_cast<int>(MediaRole::Rating):
        return item.rating;

    case static_cast<int>(MediaRole::Memo):
        return item.memo;

    case static_cast<int>(MediaRole::DateAdded):
        return item.dateAdded;

    default:
        return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles[static_cast<int>(MediaRole::Id)]            = "mediaId";
    roles[static_cast<int>(MediaRole::FilePath)]      = "filePath";
    roles[static_cast<int>(MediaRole::ThumbnailPath)] = "thumbnailPath";
    roles[static_cast<int>(MediaRole::Rating)]        = "rating";
    roles[static_cast<int>(MediaRole::Memo)]          = "memo";
    roles[static_cast<int>(MediaRole::DateAdded)]     = "dateAdded";
    return roles;
}

// ============================================================
// Public API
// ============================================================

void PlaylistModel::loadFromDatabase(DatabaseManager& db)
{
    beginResetModel();
    // Thumbnail paths may have changed (re-scan / regenerated sheets), so
    // drop the cache and let it refill lazily on the next paint.
    m_thumbCache.clear();
    m_allItems = db.getAllMedia();
    qDebug() << "[Model] loadFromDatabase: received" << m_allItems.size() << "item(s) from DB";
    applyFilter();
    qDebug() << "[Model] loadFromDatabase: filtered size =" << m_filteredItems.size()
             << "| filter query ='" << m_filterQuery << "'";
    endResetModel();
}

void PlaylistModel::setFilter(const QString& query)
{
    if (m_filterQuery == query)
        return;

    m_filterQuery = query;

    beginResetModel();
    applyFilter();
    endResetModel();
}

void PlaylistModel::setMinRating(int minRating)
{
    const int clamped = qBound(0, minRating, 100);
    if (m_minRating == clamped)
        return;

    m_minRating = clamped;

    beginResetModel();
    applyFilter();
    endResetModel();
}

void PlaylistModel::updateItem(const MediaItem& updated)
{
    // Sync into the master list
    auto it = std::ranges::find_if(m_allItems,
        [id = updated.id](const MediaItem& mi) { return mi.id == id; });

    if (it != m_allItems.end())
        *it = updated;

    // Re-derive the filtered view so the display stays consistent
    beginResetModel();
    applyFilter();
    endResetModel();
}

std::optional<MediaItem> PlaylistModel::itemAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_filteredItems.size()))
        return std::nullopt;

    return m_filteredItems[static_cast<std::size_t>(row)];
}

int PlaylistModel::rowForId(int id) const
{
    for (std::size_t i = 0; i < m_filteredItems.size(); ++i)
    {
        if (m_filteredItems[i].id == id)
            return static_cast<int>(i);
    }
    return -1;
}

// ============================================================
// Private helpers
// ============================================================

void PlaylistModel::applyFilter()
{
    const bool hasText   = !m_filterQuery.isEmpty();
    const bool hasRating = m_minRating > 0;

    if (!hasText && !hasRating)
    {
        m_filteredItems = m_allItems;
        return;
    }

    const QString lower = m_filterQuery.toLower();

    m_filteredItems.clear();
    for (const auto& item : m_allItems)
    {
        if (hasText   && !item.title.toLower().contains(lower)) continue;
        if (hasRating && item.rating < m_minRating)             continue;
        m_filteredItems.push_back(item);
    }
}

// ============================================================
// Thumbnail builder (Qt::DecorationRole) — lazy + cached.
//
// Keyed by thumbnailPath so the same scaled pixmap is reused for every
// repaint (and across items that happen to share a sheet). The empty
// path maps to a single shared placeholder so missing-thumbnail cells
// still render a tidy box instead of a broken/blank icon.
// ============================================================

QPixmap PlaylistModel::thumbnailFor(const MediaItem& item) const
{
    const QString key = item.thumbnailPath;

    if (auto it = m_thumbCache.constFind(key); it != m_thumbCache.constEnd())
        return it.value();

    QPixmap result;

    if (!key.isEmpty() && QFileInfo::exists(key))
    {
        QPixmap src(key);
        if (!src.isNull())
            result = src.scaled(kThumbW, kThumbH,
                                 Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
    }

    // Fall back to a generated placeholder: a dark rounded rect with a
    // theme-ish border and a simple ▶ glyph, so empty/broken thumbnails
    // don't leave grid cells blank.
    if (result.isNull())
    {
        QPixmap ph(kThumbW, kThumbH);
        ph.fill(Qt::transparent);

        QPainter p(&ph);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box(0.5, 0.5, kThumbW - 1.0, kThumbH - 1.0);
        p.setBrush(QColor(0x26, 0x2a, 0x30));        // dark fill
        p.setPen(QPen(QColor(0x3a, 0x3f, 0x47), 1)); // theme-ish border
        p.drawRoundedRect(box, 6, 6);

        // Centered play triangle.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x6a, 0x70, 0x7a));
        const double cx = kThumbW / 2.0;
        const double cy = kThumbH / 2.0;
        const QPointF tri[3] = {
            { cx - 9, cy - 11 },
            { cx - 9, cy + 11 },
            { cx + 13, cy      },
        };
        p.drawPolygon(tri, 3);
        p.end();

        result = ph;
    }

    m_thumbCache.insert(key, result);
    return result;
}
