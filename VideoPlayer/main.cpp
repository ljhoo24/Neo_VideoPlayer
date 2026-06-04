#include <QApplication>
#include <QStringList>
#include <QStyleFactory>
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
// Dark Fusion palette — applied globally before the window opens
// ============================================================
static void applyDarkPalette(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window,          QColor(42,  42,  42));
    p.setColor(QPalette::WindowText,      QColor(220, 220, 220));
    p.setColor(QPalette::Base,            QColor(28,  28,  28));
    p.setColor(QPalette::AlternateBase,   QColor(48,  48,  48));
    p.setColor(QPalette::ToolTipBase,     QColor(42,  42,  42));
    p.setColor(QPalette::ToolTipText,     QColor(220, 220, 220));
    p.setColor(QPalette::Text,            QColor(220, 220, 220));
    p.setColor(QPalette::Button,          QColor(58,  58,  58));
    p.setColor(QPalette::ButtonText,      QColor(220, 220, 220));
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Highlight,       QColor(42,  130, 218));
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::Link,            QColor(42,  130, 218));

    // Disabled colours — slightly dimmer
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor(120, 120, 120));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));

    app.setPalette(p);
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
    app.setApplicationVersion("1.0.0");

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

    applyDarkPalette(app);

    try
    {
        MainWindow window;
        window.show();

        // Windows passes the media path as argv[1] when the app is
        // launched via a file association / "Open with" — play it.
        const QStringList args = app.arguments();
        if (args.size() > 1)
            window.openExternalFile(args.at(1));

        return app.exec();
    }
    catch (const std::exception& ex)
    {
        QMessageBox::critical(nullptr, "Fatal Error", ex.what());
        return EXIT_FAILURE;
    }
}
