#include "DatabaseManager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QUuid>
#include <QDebug>

// ============================================================
// Constructor / Destructor
// ============================================================

DatabaseManager::DatabaseManager()
    : m_connectionName(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

DatabaseManager::~DatabaseManager()
{
    if (m_db.isOpen())
        m_db.close();

    // Qt requires the QSqlDatabase handle to be invalidated (no references left)
    // before removeDatabase() is called, otherwise it emits a resource-leak warning.
    m_db = QSqlDatabase();

    QSqlDatabase::removeDatabase(m_connectionName);
}

// ============================================================
// Initialization
// ============================================================

bool DatabaseManager::initialize(const QString& dbPath)
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open())
    {
        qWarning() << "[DB] Failed to open:" << m_db.lastError().text();
        return false;
    }

    // Enable WAL mode for better concurrent performance
    QSqlQuery pragmaQuery(m_db);
    pragmaQuery.exec("PRAGMA journal_mode=WAL");

    m_initialized = createTables();
    return m_initialized;
}

bool DatabaseManager::createTables()
{
    QSqlQuery q(m_db);

    const bool ok = q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS media_files (
            id             INTEGER PRIMARY KEY AUTOINCREMENT,
            title          TEXT    NOT NULL,
            file_path      TEXT    UNIQUE NOT NULL,
            thumbnail_path TEXT    NOT NULL DEFAULT '',
            rating         INTEGER NOT NULL DEFAULT 0
                               CHECK(rating >= 0 AND rating <= 100),
            memo           TEXT    NOT NULL DEFAULT '',
            date_added     TEXT    NOT NULL
                               DEFAULT (datetime('now', 'localtime'))
        )
    )SQL");

    if (!ok)
        qWarning() << "[DB] createTables failed:" << q.lastError().text();

    return ok;
}

// ============================================================
// Write operations
// ============================================================

bool DatabaseManager::addMediaFile(const QString& filePath,
                                   const QString& thumbnailPath)
{
    if (!m_initialized)
    {
        qWarning() << "[DB] addMediaFile called but DB not initialised";
        return false;
    }

    const QString title = QFileInfo(filePath).completeBaseName();

    QSqlQuery q(m_db);
    q.prepare(R"SQL(
        INSERT OR IGNORE INTO media_files (title, file_path, thumbnail_path)
        VALUES (:title, :path, :thumb)
    )SQL");
    q.bindValue(":title", title);
    q.bindValue(":path",  filePath);
    // Explicitly convert null QString → empty string so Qt doesn't bind SQL NULL,
    // which would violate the NOT NULL constraint and silently reject the INSERT.
    q.bindValue(":thumb", thumbnailPath.isNull() ? QStringLiteral("") : thumbnailPath);

    if (!q.exec())
    {
        qWarning() << "[DB] addMediaFile failed:" << q.lastError().text()
                   << "| path:" << filePath;
        return false;
    }

    // numRowsAffected() == 0 → UNIQUE constraint fired (row already existed)
    // numRowsAffected() == 1 → row was newly inserted
    const int  affected  = q.numRowsAffected();
    const bool inserted  = (affected > 0);
    qDebug() << "[DB] addMediaFile numRowsAffected=" << affected
             << "lastInsertId=" << q.lastInsertId()
             << (inserted ? "INSERTED" : "already exists")
             << filePath;
    return inserted;
}

bool DatabaseManager::updateRating(int id, int rating)
{
    if (!m_initialized)
        return false;

    QSqlQuery q(m_db);
    q.prepare("UPDATE media_files SET rating = :rating WHERE id = :id");
    q.bindValue(":rating", rating);
    q.bindValue(":id",     id);

    if (!q.exec())
    {
        qWarning() << "[DB] updateRating failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::updateMemo(int id, const QString& memo)
{
    if (!m_initialized)
        return false;

    QSqlQuery q(m_db);
    q.prepare("UPDATE media_files SET memo = :memo WHERE id = :id");
    q.bindValue(":memo", memo);
    q.bindValue(":id",   id);

    if (!q.exec())
    {
        qWarning() << "[DB] updateMemo failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::updateThumbnail(int id, const QString& thumbnailPath)
{
    if (!m_initialized)
        return false;

    QSqlQuery q(m_db);
    q.prepare("UPDATE media_files SET thumbnail_path = :thumb WHERE id = :id");
    q.bindValue(":thumb", thumbnailPath.isNull() ? QStringLiteral("") : thumbnailPath);
    q.bindValue(":id",    id);

    if (!q.exec())
    {
        qWarning() << "[DB] updateThumbnail failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::bumpToFront(int id)
{
    if (!m_initialized)
        return false;

    QSqlQuery q(m_db);
    q.prepare("UPDATE media_files "
              "SET date_added = datetime('now', 'localtime') "
              "WHERE id = :id");
    q.bindValue(":id", id);

    if (!q.exec())
    {
        qWarning() << "[DB] bumpToFront failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::removeMediaFile(int id)
{
    if (!m_initialized)
        return false;

    QSqlQuery q(m_db);
    q.prepare("DELETE FROM media_files WHERE id = :id");
    q.bindValue(":id", id);

    if (!q.exec())
    {
        qWarning() << "[DB] removeMediaFile failed:" << q.lastError().text();
        return false;
    }
    return true;
}

// ============================================================
// Read operations
// ============================================================

MediaItem DatabaseManager::rowToItem(const QSqlQuery& q) const
{
    return MediaItem
    {
        .id            = q.value("id").toInt(),
        .title         = q.value("title").toString(),
        .filePath      = q.value("file_path").toString(),
        .thumbnailPath = q.value("thumbnail_path").toString(),
        .rating        = q.value("rating").toInt(),
        .memo          = q.value("memo").toString(),
        .dateAdded     = q.value("date_added").toString(),
    };
}

std::vector<MediaItem> DatabaseManager::getAllMedia() const
{
    if (!m_initialized)
    {
        qWarning() << "[DB] getAllMedia: not initialized";
        return {};
    }

    // ---- Diagnostic: how many rows are actually in the table? ----
    {
        QSqlQuery cq(m_db);
        if (cq.exec("SELECT COUNT(*) FROM media_files") && cq.next())
            qDebug() << "[DB] getAllMedia: table row count =" << cq.value(0).toInt();
        else
            qWarning() << "[DB] getAllMedia: COUNT query failed:" << cq.lastError().text();
    }

    QSqlQuery q(m_db);
    if (!q.exec("SELECT * FROM media_files ORDER BY date_added DESC"))
    {
        qWarning() << "[DB] getAllMedia SELECT failed:" << q.lastError().text();
        return {};
    }

    std::vector<MediaItem> items;
    while (q.next())
        items.push_back(rowToItem(q));

    qDebug() << "[DB] getAllMedia returning" << items.size() << "item(s)";
    return items;
}

std::vector<MediaItem> DatabaseManager::searchMedia(const QString& query) const
{
    if (!m_initialized)
        return {};

    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM media_files "
              "WHERE title LIKE :query "
              "ORDER BY date_added DESC");
    q.bindValue(":query", "%" + query + "%");

    if (!q.exec())
    {
        qWarning() << "[DB] searchMedia failed:" << q.lastError().text();
        return {};
    }

    std::vector<MediaItem> items;
    while (q.next())
        items.push_back(rowToItem(q));

    return items;
}

std::optional<MediaItem> DatabaseManager::getMediaById(int id) const
{
    if (!m_initialized)
        return std::nullopt;

    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM media_files WHERE id = :id");
    q.bindValue(":id", id);

    if (!q.exec() || !q.next())
        return std::nullopt;

    return rowToItem(q);
}

std::optional<MediaItem> DatabaseManager::getMediaByPath(const QString& path) const
{
    if (!m_initialized)
        return std::nullopt;

    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM media_files WHERE file_path = :path");
    q.bindValue(":path", path);

    if (!q.exec() || !q.next())
        return std::nullopt;

    return rowToItem(q);
}
