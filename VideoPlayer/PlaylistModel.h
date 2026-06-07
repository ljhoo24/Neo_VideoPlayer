#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPixmap>
#include <vector>
#include <optional>
#include "DatabaseManager.h"

// ============================================================
// PlaylistModel
//   Qt model backed by a std::vector<MediaItem>.
//   Supports live text filtering without re-querying the DB.
// ============================================================
class PlaylistModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    // Custom roles beyond Qt::DisplayRole
    enum class MediaRole : int
    {
        Id            = Qt::UserRole + 1,
        FilePath      = Qt::UserRole + 2,
        ThumbnailPath = Qt::UserRole + 3,
        Rating        = Qt::UserRole + 4,
        Memo          = Qt::UserRole + 5,
        DateAdded     = Qt::UserRole + 6,
    };

    explicit PlaylistModel(QObject* parent = nullptr);

    // ---- QAbstractListModel interface ----
    [[nodiscard]] int     rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // ---- Public API ----
    void loadFromDatabase(DatabaseManager& db);
    void setFilter(const QString& query);
    void setMinRating(int minRating);
    [[nodiscard]] int minRating() const noexcept { return m_minRating; }
    void updateItem(const MediaItem& updated);

    [[nodiscard]] std::optional<MediaItem> itemAt(int row) const;

    // Find the row of the entry with the given media id in the current
    // filtered view. Returns -1 if no match (e.g. the entry was deleted,
    // or it's hidden by the active filter). Used to restore the
    // last-played selection across application restarts.
    [[nodiscard]] int rowForId(int id) const;

private:
    std::vector<MediaItem> m_allItems;
    std::vector<MediaItem> m_filteredItems;
    QString                m_filterQuery;
    int                    m_minRating{0};

    // Thumbnail cache for the grid (Qt::DecorationRole). Scaling an
    // image from disk on every repaint would stutter the view, so we
    // cache the scaled QPixmap keyed by thumbnailPath. Empty key holds
    // the shared placeholder. mutable: filled lazily inside const data().
    mutable QHash<QString, QPixmap> m_thumbCache;

    // Build (and cache) the scaled thumbnail for an item, or a generated
    // placeholder when the path is empty / unreadable.
    [[nodiscard]] QPixmap thumbnailFor(const MediaItem& item) const;

    void applyFilter();
};
