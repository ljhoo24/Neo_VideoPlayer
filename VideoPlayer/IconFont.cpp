#include "IconFont.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QFont>

namespace
{
QString g_family;
}

QString Icons::loadFont()
{
    if (!g_family.isEmpty())
        return g_family;

    const int id = QFontDatabase::addApplicationFont(
        QStringLiteral(":/fonts/MaterialIcons-Regular.ttf"));
    if (id >= 0)
    {
        const QStringList fams = QFontDatabase::applicationFontFamilies(id);
        if (!fams.isEmpty())
            g_family = fams.first();
    }
    return g_family;
}

QString Icons::family()
{
    return g_family;
}

QIcon Icons::icon(char16_t codepoint, const QColor& color, int px)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    const int   dim = static_cast<int>(px * dpr);

    QPixmap pm(dim, dim);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QFont f(g_family);
    f.setPixelSize(px);
    p.setFont(f);
    p.setPen(color);
    // Draw in logical coordinates (pixmap dpr already set).
    p.drawText(QRectF(0, 0, px, px), Qt::AlignCenter,
               QString(QChar(static_cast<ushort>(codepoint))));
    p.end();

    return QIcon(pm);
}
