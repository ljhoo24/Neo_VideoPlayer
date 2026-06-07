#pragma once

#include <QString>
#include <QSqlDatabase>
#include <vector>
#include <optional>
#include <cstdint>

// ============================================================
// MediaItem — plain data struct representing one playlist entry
// ============================================================
struct MediaItem
{
    int     id{0};
    QString title;
    QString filePath;
    QString thumbnailPath;
    int     rating{0};
    QString memo;
    QString dateAdded;
    double  resumePos{0.0};   // last playback position in seconds ("이어보기")
};

// ============================================================
// DatabaseManager
//   Wraps all SQLite operations via Qt's QSQLITE driver.
//   Each instance owns an independent DB connection.
// ============================================================
class DatabaseManager
{
public:
    DatabaseManager();
    ~DatabaseManager();

    // Non-copyable, movable
    DatabaseManager(const DatabaseManager&)            = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    DatabaseManager(DatabaseManager&&)                 = default;
    DatabaseManager& operator=(DatabaseManager&&)      = default;

    [[nodiscard]] bool initialize(const QString& dbPath);
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    // ---- Write operations ----
    [[nodiscard]] bool addMediaFile(const QString& filePath,
                                    const QString& thumbnailPath = QLatin1String(""));
    [[nodiscard]] bool updateRating(int id, int rating);
    [[nodiscard]] bool updateMemo(int id, const QString& memo);
    [[nodiscard]] bool setResumePos(int id, double seconds);
    [[nodiscard]] bool updateThumbnail(int id, const QString& thumbnailPath);
    [[nodiscard]] bool removeMediaFile(int id);

    // Bump an entry's date_added to "now" so it sorts to the front of the
    // playlist (which is ordered by date_added DESC). Used when a file is
    // opened from outside the app so the just-played video jumps to the top.
    [[nodiscard]] bool bumpToFront(int id);

    // ---- Read operations ----
    [[nodiscard]] std::vector<MediaItem> getAllMedia()                          const;
    [[nodiscard]] std::vector<MediaItem> searchMedia(const QString& query)     const;
    [[nodiscard]] std::optional<MediaItem> getMediaById(int id)                const;
    [[nodiscard]] std::optional<MediaItem> getMediaByPath(const QString& path) const;

private:
    QSqlDatabase m_db;
    QString      m_connectionName;
    bool         m_initialized{false};

    [[nodiscard]] bool      createTables();
    // Idempotent schema upgrade for DBs created before a column existed.
    // Runs on every launch right after createTables(); a no-op once applied.
    void                    migrateSchema();
    [[nodiscard]] MediaItem rowToItem(const class QSqlQuery& q) const;
};
