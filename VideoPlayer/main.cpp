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
    app.setApplicationVersion("1.0.8");

    // ---- Open log file before anything else ----
    {
        const QString logDir = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        QDir().mkpath(logDir);
        g_logFile.setFileName(logDir + "/app.log");
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
    {
        QLocalSocket probe;
        probe.connectToServer(kIpcServerName);
        if (probe.waitForConnected(300))
        {
            qDebug() << "[SingleInstance] existing instance found — forwarding:" << openPath;
            probe.write(openPath.toUtf8());
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
        if (!ipcServer.listen(kIpcServerName))
            qWarning() << "[SingleInstance] listen failed:" << ipcServer.errorString();

        QObject::connect(&ipcServer, &QLocalServer::newConnection,
                         [&ipcServer, &window]()
        {
            QLocalSocket* conn = ipcServer.nextPendingConnection();
            if (!conn)
                return;
            if (conn->waitForReadyRead(1000))
            {
                const QString path = QString::fromUtf8(conn->readAll()).trimmed();
                qDebug() << "[SingleInstance] received open request:" << path;

                // Restore + bring the existing window to the foreground.
                if (window.isMinimized())
                    window.showNormal();
                window.show();
                window.raise();
                window.activateWindow();

                if (!path.isEmpty())
                    window.openExternalFile(path);
            }
            conn->disconnectFromServer();
            conn->deleteLater();
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
