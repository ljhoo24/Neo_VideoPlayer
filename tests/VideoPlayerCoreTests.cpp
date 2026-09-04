#include "DatabaseManager.h"
#include "PlaylistModel.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include <memory>

class VideoPlayerCoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void databaseRejectsDuplicatesAndFindsRows();
    void metadataUpdateIsAtomic();
    void bookmarksFollowMediaLifetime();
    void legacyDatabaseIsMigrated();
    void playlistFilteringAndUpdatesStayConsistent();
    void thumbnailCacheRefreshesAnOverwrittenPath();

private:
    QString mediaPath(const QString& fileName) const;
    MediaItem addMedia(const QString& fileName,
                       const QString& thumbnailPath = {});

    std::unique_ptr<QTemporaryDir> m_tempDir;
    std::unique_ptr<DatabaseManager> m_db;
};

void VideoPlayerCoreTests::init()
{
    m_tempDir = std::make_unique<QTemporaryDir>();
    QVERIFY2(m_tempDir->isValid(), "Could not create the test directory");

    m_db = std::make_unique<DatabaseManager>();
    QVERIFY2(m_db->initialize(m_tempDir->filePath("library.sqlite")),
             "Could not initialize the test database");
}

void VideoPlayerCoreTests::cleanup()
{
    // Close SQLite before QTemporaryDir removes its database and WAL files.
    m_db.reset();
    m_tempDir.reset();
}

QString VideoPlayerCoreTests::mediaPath(const QString& fileName) const
{
    return m_tempDir->filePath(fileName);
}

MediaItem VideoPlayerCoreTests::addMedia(const QString& fileName,
                                         const QString& thumbnailPath)
{
    const QString path = mediaPath(fileName);
    if (!m_db->addMediaFile(path, thumbnailPath))
        return {};
    const auto item = m_db->getMediaByPath(path);
    return item.value_or(MediaItem{});
}

void VideoPlayerCoreTests::databaseRejectsDuplicatesAndFindsRows()
{
    const QString path = mediaPath("Alpha.Movie.mp4");
    QVERIFY(m_db->addMediaFile(path));
    QVERIFY(!m_db->addMediaFile(path));

    const auto byPath = m_db->getMediaByPath(path);
    QVERIFY(byPath.has_value());
    QVERIFY(byPath->id > 0);
    QCOMPARE(byPath->title, QString("Alpha.Movie"));

    const auto byId = m_db->getMediaById(byPath->id);
    QVERIFY(byId.has_value());
    QCOMPARE(byId->filePath, path);
    QCOMPARE(m_db->getAllMedia().size(), std::size_t{1});
}

void VideoPlayerCoreTests::metadataUpdateIsAtomic()
{
    const MediaItem item = addMedia("metadata.mkv");
    QVERIFY(item.id > 0);

    const QString injectionText =
        QStringLiteral("memo'); DROP TABLE media_files; --");
    QVERIFY(m_db->updateMetadata(item.id, 87, injectionText));

    auto updated = m_db->getMediaById(item.id);
    QVERIFY(updated.has_value());
    QCOMPARE(updated->rating, 87);
    QCOMPARE(updated->memo, injectionText);

    // The CHECK constraint must reject the entire statement: memo cannot be
    // partially changed when the rating is invalid.
    QVERIFY(!m_db->updateMetadata(item.id, 101, "must not be stored"));
    updated = m_db->getMediaById(item.id);
    QVERIFY(updated.has_value());
    QCOMPARE(updated->rating, 87);
    QCOMPARE(updated->memo, injectionText);
    QVERIFY(!m_db->updateMetadata(item.id + 9999, 10, "missing row"));
}

void VideoPlayerCoreTests::bookmarksFollowMediaLifetime()
{
    const MediaItem item = addMedia("bookmarks.webm");
    QVERIFY(item.id > 0);

    QVERIFY(m_db->addBookmark(item.id, 12.5, "later") > 0);
    QVERIFY(m_db->addBookmark(item.id, 5.0, "first") > 0);
    QCOMPARE(m_db->addBookmark(item.id + 9999, 1.0, "orphan"), -1);

    const auto marks = m_db->getBookmarks(item.id);
    QCOMPARE(marks.size(), std::size_t{2});
    QCOMPARE(marks[0].position, 5.0);
    QCOMPARE(marks[0].note, QString("first"));
    QCOMPARE(marks[1].position, 12.5);

    QVERIFY(m_db->removeMediaFile(item.id));
    QVERIFY(!m_db->getMediaById(item.id).has_value());
    QVERIFY(m_db->getBookmarks(item.id).empty());
    QVERIFY(!m_db->removeMediaFile(item.id));
}

void VideoPlayerCoreTests::legacyDatabaseIsMigrated()
{
    const QString legacyPath = m_tempDir->filePath("legacy.sqlite");
    const QString connection =
        QUuid::createUuid().toString(QUuid::WithoutBraces);

    {
        QSqlDatabase legacy = QSqlDatabase::addDatabase("QSQLITE", connection);
        legacy.setDatabaseName(legacyPath);
        QVERIFY(legacy.open());

        QSqlQuery q(legacy);
        QVERIFY(q.exec(R"SQL(
            CREATE TABLE media_files (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL,
                file_path TEXT UNIQUE NOT NULL,
                thumbnail_path TEXT NOT NULL DEFAULT '',
                rating INTEGER NOT NULL DEFAULT 0,
                memo TEXT NOT NULL DEFAULT '',
                date_added TEXT NOT NULL DEFAULT (datetime('now','localtime'))
            )
        )SQL"));
        q.prepare("INSERT INTO media_files (title, file_path) VALUES (?, ?)");
        q.addBindValue("Legacy");
        q.addBindValue(mediaPath("legacy.mp4"));
        QVERIFY(q.exec());

        legacy.close();
    }
    QSqlDatabase::removeDatabase(connection);

    DatabaseManager migrated;
    QVERIFY(migrated.initialize(legacyPath));
    const auto item = migrated.getMediaByPath(mediaPath("legacy.mp4"));
    QVERIFY(item.has_value());
    QCOMPARE(item->resumePos, 0.0);
    QVERIFY(migrated.setResumePos(item->id, 42.25));
    QCOMPARE(migrated.getMediaById(item->id)->resumePos, 42.25);
}

void VideoPlayerCoreTests::playlistFilteringAndUpdatesStayConsistent()
{
    MediaItem alpha = addMedia("Alpha.mp4");
    MediaItem beta = addMedia("Beta.mp4");
    QVERIFY(alpha.id > 0);
    QVERIFY(beta.id > 0);
    QVERIFY(m_db->updateMetadata(alpha.id, 90, "favorite"));
    QVERIFY(m_db->updateMetadata(beta.id, 20, "later"));

    PlaylistModel model;
    QAbstractItemModelTester modelTester(
        &model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.loadFromDatabase(*m_db);
    QCOMPARE(model.rowCount(), 2);

    model.setFilter("ALPHA");
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0),
                        static_cast<int>(PlaylistModel::MediaRole::Id)).toInt(),
             alpha.id);

    model.setMinRating(95);
    QCOMPARE(model.rowCount(), 0);
    model.setMinRating(80);
    QCOMPARE(model.rowCount(), 1);

    model.setFilter({});
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowForId(beta.id), -1);

    alpha = *m_db->getMediaById(alpha.id);
    alpha.title = "Renamed locally";
    model.updateItem(alpha);
    model.setFilter("renamed");
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.itemAt(0)->id, alpha.id);
    QVERIFY(!model.itemAt(-1).has_value());
}

void VideoPlayerCoreTests::thumbnailCacheRefreshesAnOverwrittenPath()
{
    const QString thumbnailPath = m_tempDir->filePath("thumbnail.png");
    QImage red(16, 9, QImage::Format_RGB32);
    red.fill(Qt::red);
    QVERIFY(red.save(thumbnailPath));

    const MediaItem item = addMedia("thumbnail.mp4", thumbnailPath);
    QVERIFY(item.id > 0);

    PlaylistModel model;
    model.loadFromDatabase(*m_db);
    const auto decorationRole = Qt::DecorationRole;
    QPixmap first = qvariant_cast<QPixmap>(
        model.data(model.index(0), decorationRole));
    QVERIFY(!first.isNull());
    QCOMPARE(first.toImage().pixelColor(first.width() / 2,
                                         first.height() / 2),
             QColor(Qt::red));

    QImage blue(16, 9, QImage::Format_RGB32);
    blue.fill(Qt::blue);
    QVERIFY(blue.save(thumbnailPath));
    model.updateItem(item); // same path, new file contents

    QPixmap second = qvariant_cast<QPixmap>(
        model.data(model.index(0), decorationRole));
    QVERIFY(!second.isNull());
    QCOMPARE(second.toImage().pixelColor(second.width() / 2,
                                          second.height() / 2),
             QColor(Qt::blue));
}

QTEST_MAIN(VideoPlayerCoreTests)
#include "VideoPlayerCoreTests.moc"
