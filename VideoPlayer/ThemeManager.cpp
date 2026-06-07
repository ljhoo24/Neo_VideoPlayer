#include "ThemeManager.h"

#include <QApplication>
#include <QStyleFactory>
#include <QStyleHints>
#include <QPalette>
#include <QSettings>
#include <QFile>
#include <QHash>
#include <QString>
#include <QDebug>

// ============================================================
// Internal state (process-global — one theme for the whole app)
// ============================================================
namespace
{
ThemeManager::Theme g_theme  = ThemeManager::Theme::Dark;
QColor              g_accent = QColor(0x4f, 0x93, 0xff);

// ---- Static (accent-independent) token sets per palette ----
// Each maps an @TOKEN@ name (without the @ delimiters) to a hex string.
// ACCENT / ACCENT_HOVER / ACCENT_PRESS are injected at substitution time
// from g_accent so the user's choice flows through both palettes.
struct Palette
{
    QString bg, text, textMuted, surface, elevated, border, borderStrong,
            textDisabled, onAccent, altBase, hover, groupBg, barBg;
};

const Palette kDark {
    /* bg            */ "#1e1f22",
    /* text          */ "#e6e7ea",
    /* textMuted     */ "#9aa0a8",
    /* surface       */ "#26282c",
    /* elevated      */ "#2f3137",
    /* border        */ "#3a3d44",
    /* borderStrong  */ "#4a4e56",
    /* textDisabled  */ "#6b7079",
    /* onAccent      */ "#ffffff",
    /* altBase       */ "#292b30",
    /* hover         */ "#313438",
    /* groupBg       */ "#222428",
    /* barBg         */ "#1b1c1f",
};

// Tasteful light theme — soft greys, near-white surfaces, dark text.
const Palette kLight {
    /* bg            */ "#f4f5f7",
    /* text          */ "#1d1f23",
    /* textMuted     */ "#6b7079",
    /* surface       */ "#ffffff",
    /* elevated      */ "#eceef1",
    /* border        */ "#d0d4da",
    /* borderStrong  */ "#b6bcc4",
    /* textDisabled  */ "#a8adb5",
    /* onAccent      */ "#ffffff",
    /* altBase       */ "#f0f1f4",
    /* hover         */ "#e4e7ec",
    /* groupBg       */ "#fbfbfc",
    /* barBg         */ "#eceef1",
};

// Resolve the EFFECTIVE light/dark choice. Dark/Light are literal; Auto
// asks the OS via QStyleHints::colorScheme() (Qt 6.5+). Unknown → dark.
bool effectiveIsDark()
{
    switch (g_theme)
    {
    case ThemeManager::Theme::Light: return false;
    case ThemeManager::Theme::Dark:  return true;
    case ThemeManager::Theme::Auto:
    default:
        if (qApp)
            return qApp->styleHints()->colorScheme() != Qt::ColorScheme::Light;
        return true;
    }
}

const Palette& activePalette()
{
    return effectiveIsDark() ? kDark : kLight;
}

// Build the full @TOKEN@ → hex map for the active theme, folding in the
// accent (and its derived hover/press shades). lighter()/darker() give a
// cheap, consistent way to derive the two accent variants from any user
// colour without a second picker.
QHash<QString, QString> buildTokens()
{
    const Palette& p = activePalette();

    QHash<QString, QString> t;
    t["BG"]            = p.bg;
    t["TEXT"]          = p.text;
    t["TEXT_MUTED"]    = p.textMuted;
    t["SURFACE"]       = p.surface;
    t["ELEVATED"]      = p.elevated;
    t["BORDER"]        = p.border;
    t["BORDER_STRONG"] = p.borderStrong;
    t["TEXT_DISABLED"] = p.textDisabled;
    t["ON_ACCENT"]     = p.onAccent;
    t["ALT_BASE"]      = p.altBase;
    t["HOVER"]         = p.hover;
    t["GROUP_BG"]      = p.groupBg;
    t["BAR_BG"]        = p.barBg;

    t["ACCENT"]        = g_accent.name();
    t["ACCENT_HOVER"]  = g_accent.lighter(118).name();
    t["ACCENT_PRESS"]  = g_accent.darker(115).name();

    return t;
}

QColor hex(const QString& s) { return QColor(s); }
}

namespace ThemeManager
{
// ============================================================
// Persistence
// ============================================================
void load()
{
    QSettings s;
    const QString theme = s.value("ui/theme", "dark").toString();
    if (theme.compare("light", Qt::CaseInsensitive) == 0)
        g_theme = Theme::Light;
    else if (theme.compare("auto", Qt::CaseInsensitive) == 0)
        g_theme = Theme::Auto;
    else
        g_theme = Theme::Dark;

    const QColor a(s.value("ui/accent", "#4f93ff").toString());
    if (a.isValid())
        g_accent = a;
}

// ============================================================
// Apply — substitute tokens into the template, set stylesheet + palette
// ============================================================
void apply(QApplication& app)
{
    // ---- Native palette first (Fusion) so focus rings / disabled text
    //      blend with the stylesheet even on bits QSS doesn't reach. ----
    app.setStyle(QStyleFactory::create("Fusion"));

    const Palette& p = activePalette();
    QPalette pal;
    pal.setColor(QPalette::Window,          hex(p.bg));
    pal.setColor(QPalette::WindowText,      hex(p.text));
    pal.setColor(QPalette::Base,            hex(p.surface));
    pal.setColor(QPalette::AlternateBase,   hex(p.altBase));
    pal.setColor(QPalette::ToolTipBase,     hex(p.elevated));
    pal.setColor(QPalette::ToolTipText,     hex(p.text));
    pal.setColor(QPalette::Text,            hex(p.text));
    pal.setColor(QPalette::Button,          hex(p.elevated));
    pal.setColor(QPalette::ButtonText,      hex(p.text));
    pal.setColor(QPalette::BrightText,      Qt::red);
    pal.setColor(QPalette::Highlight,       g_accent);
    pal.setColor(QPalette::HighlightedText, hex(p.onAccent));
    pal.setColor(QPalette::Link,            g_accent);
    pal.setColor(QPalette::Disabled, QPalette::Text,       hex(p.textDisabled));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, hex(p.textDisabled));
    app.setPalette(pal);

    // ---- Read the tokenized template and substitute ----
    QFile qss(QStringLiteral(":/theme.qss"));
    if (!qss.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "[Theme] could not load :/theme.qss";
        return;
    }
    QString sheet = QString::fromUtf8(qss.readAll());

    const QHash<QString, QString> tokens = buildTokens();
    for (auto it = tokens.cbegin(); it != tokens.cend(); ++it)
        sheet.replace('@' + it.key() + '@', it.value());

    app.setStyleSheet(sheet);
}

// ============================================================
// Setters — update state, persist, re-apply
// ============================================================
void setTheme(Theme theme)
{
    g_theme = theme;
    const char* name = theme == Theme::Light ? "light"
                     : theme == Theme::Auto  ? "auto"
                                             : "dark";
    QSettings().setValue("ui/theme", name);
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()))
        apply(*app);
}

void setAccent(const QColor& accent)
{
    if (!accent.isValid())
        return;
    g_accent = accent;
    QSettings().setValue("ui/accent", accent.name());
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()))
        apply(*app);
}

// ============================================================
// State accessors
// ============================================================
Theme  theme()  { return g_theme; }
bool   isDark() { return effectiveIsDark(); }
QColor accent() { return g_accent; }

// ============================================================
// Icon colours — drawn glyphs (Icons::icon) track the theme
// ============================================================
QColor iconColor()
{
    // Normal glyph: light grey on dark, dark grey on light. Mirrors the
    // old hard-coded QColor(0xcf,0xd3,0xda) on the dark theme.
    return isDark() ? QColor(0xcf, 0xd3, 0xda) : QColor(0x3a, 0x3d, 0x44);
}

QColor iconMuted()
{
    return hex(activePalette().textMuted);
}

QColor onAccent()
{
    return hex(activePalette().onAccent);
}

QColor danger()
{
    // Soft red on dark; a stronger red on light so it stays legible.
    return isDark() ? QColor(0xe0, 0x8a, 0x8a) : QColor(0xc0, 0x39, 0x2b);
}
}
