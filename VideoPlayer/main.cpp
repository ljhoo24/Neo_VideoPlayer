#include <QApplication>
#include <QStringList>
#include <QStyleFactory>
#include <QLocalServer>
#include <QLocalSocket>
#include <QFileInfo>
#include <QPalette>
#include <QColor>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <memory>
#include <stdexcept>

#include "MainWindow.h"
#include "IconFont.h"
#include "ThemeManager.h"

// ============================================================
// File-based message handler — captures qDebug/qWarning/qCritical
// even in Release GUI builds (where the console is not attached).
// Log is written to %APPDATA%\CustomMedia\VideoPlayer\app.log
// ============================================================
static QFile  g_logFile;
static QMutex g_logMutex;

static void fileMessageHandler(QtMsgType type,
                               const QMessageLogContext& /*ctx*/,
                               const QString& msg)
{
    QMutexLocker lock(&g_logMutex);
    if (!g_logFile.isOpen())
        return;

    QTextStream out(&g_logFile);
    const QString ts = QDateTime::currentDateTime()
                           .toString("yyyy-MM-dd HH:mm:ss.zzz");

    const char* level = "DBG";
    switch (type)
    {
    case QtWarningMsg:  level = "WRN"; break;
    case QtCriticalMsg: level = "CRT"; break;
    case QtFatalMsg:    level = "FTL"; break;
    default:            level = "DBG"; break;
    }

    out << ts << " [" << level << "] " << msg << "\n";
    out.flush();
}

// ============================================================
// Entry point
// ============================================================
int main(int argc, char* argv[])
{
    // High-DPI support (Qt6 enables this by default; explicit on Qt5)
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);

    app.setApplicationName("VideoPlayer");
    app.setOrganizationName("CustomMedia");
    app.setApplicationVersion("1.0.9");

    // ---- Open log file before anything else ----
    {
        const QString logDir = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        QDir().mkpath(logDir);
        const QString logPath = logDir + "/app.log";
        const QString oldLogPath = logDir + "/app.log.1";
        constexpr qint64 maxLogBytes = 5 * 1024 * 1024;
        if (QFileInfo(logPath).size() >= maxLogBytes)
        {
            QFile::remove(oldLogPath);
            if (!QFile::rename(logPath, oldLogPath))
                QFile::remove(logPath);
        }
        g_logFile.setFileName(logPath);
        g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    qInstallMessageHandler(fileMessageHandler);
    qDebug() << "=== VideoPlayer starting ===";
    qDebug() << "Qt version:" << qVersion();

    // ---- Resolve the file argument (argv[1]) to an absolute path now,
    //      while we still know the launch directory. Windows passes an
    //      absolute path via %1, but absolutising guards relative ones. ----
    QString openPath;
    {
        const QStringList args = app.arguments();
        if (args.size() > 1 && !args.at(1).isEmpty())
            openPath = QFileInfo(args.at(1)).absoluteFilePath();
    }

    // ---- Single-instance: if another VideoPlayer is already running,
    //      hand the file path to it and exit instead of opening a 2nd
    //      window. The first instance adds + plays it. ----
    const QString kIpcServerName = QStringLiteral("VideoPlayer.SingleInstance.CustomMedia");
    constexpr qint64 kMaxIpcPayload = 256 * 1024;
    {
        QLocalSocket probe;
        probe.connectToServer(kIpcServerName);
        if (probe.waitForConnected(300))
        {
            qDebug() << "[SingleInstance] existing instance found — forwarding:" << openPath;
            const QByteArray payload = openPath.toUtf8();
            if (payload.size() <= kMaxIpcPayload)
                probe.write(payload);
            else
                qWarning() << "[SingleInstance] path exceeds IPC limit";
            probe.flush();
            probe.waitForBytesWritten(1000);
            probe.disconnectFromServer();
            if (probe.state() != QLocalSocket::UnconnectedState)
                probe.waitForDisconnected(1000);
            return 0;   // forwarded — second process exits immediately
        }
    }

    // ---- Load embedded icon font, then load + apply the active theme.
    //      ThemeManager substitutes the chosen palette + accent into the
    //      tokenized :/theme.qss template and sets a matching QPalette. ----
    Icons::loadFont();
    ThemeManager::load();
    ThemeManager::apply(app);

    try
    {
        MainWindow window;

        // Become the single-instance server. Remove any stale socket left
        // by a crashed previous run, then listen for forwarded paths.
        QLocalServer ipcServer;
        QLocalServer::removeServer(kIpcServerName);
        // Only processes running as this OS user may send file-open requests.
        ipcServer.setSocketOptions(QLocalServer::UserAccessOption);
        if (!ipcServer.listen(kIpcServerName))
            qWarning() << "[SingleInstance] listen failed:" << ipcServer.errorString();

        QObject::connect(&ipcServer, &QLocalServer::newConnection,
                         [&ipcServer, &window]()
        {
            QLocalSocket* conn = ipcServer.nextPendingConnection();
            if (!conn)
                return;
            conn->setReadBufferSize(kMaxIpcPayload + 1);

            auto payload = std::make_shared<QByteArray>();
            auto overflow = std::make_shared<bool>(false);
            const auto consume = [conn, payload, overflow]() {
                const qint64 room = kMaxIpcPayload + 1 - payload->size();
                if (room > 0)
                    payload->append(conn->read(room));
                if (payload->size() > kMaxIpcPayload
                    || conn->bytesAvailable() > 0)
                {
                    *overflow = true;
                    conn->abort();
                }
            };

            QObject::connect(conn, &QLocalSocket::readyRead, conn, consume);
            QObject::connect(conn, &QLocalSocket::disconnected, conn,
                [conn, payload, overflow, consume, &window]() {
                    consume();
                    if (*overflow)
                    {
                        qWarning() << "[SingleInstance] rejected oversized request";
                        conn->deleteLater();
                        return;
                    }

                    const QString path = QString::fromUtf8(*payload).trimmed();
                    qDebug() << "[SingleInstance] received open request:" << path;

                    // Restore + bring the existing window to the foreground.
                    if (window.isMinimized())
                        window.showNormal();
                    window.show();
                    window.raise();
                    window.activateWindow();

                    if (!path.isEmpty())
                        window.openExternalFile(path);
                    conn->deleteLater();
                });
            consume();
        });

        window.show();

        // File handed to us on the command line (this is the first instance).
        if (!openPath.isEmpty())
            window.openExternalFile(openPath);

        return app.exec();
    }
    catch (const std::exception& ex)
    {
        QMessageBox::critical(nullptr, "Fatal Error", ex.what());
        return EXIT_FAILURE;
    }
}
