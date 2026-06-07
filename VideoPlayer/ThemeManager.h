#pragma once

#include <QColor>
#include <QString>

class QApplication;

// ============================================================
// ThemeManager
//   Central authority for the app's visual theme. Owns two named
//   palettes (Dark / Light) as @TOKEN@ → hex maps, plus a single
//   user-overridable accent colour.
//
//   The stylesheet shipped as ":/theme.qss" is a TEMPLATE: every
//   distinct colour was replaced with an @TOKEN@ placeholder.
//   apply() reads the template, substitutes the active palette's
//   tokens (and the derived accent shades), pushes the result via
//   QApplication::setStyleSheet, and sets a matching QPalette so the
//   native Fusion bits (focus rings, disabled text, …) blend in.
//
//   Why a singleton-ish namespace rather than a QObject: the state is
//   process-global (one stylesheet for the whole app) and the call
//   sites are few (main.cpp at startup, OptionsDialog on accept). A
//   plain namespace keeps it light and avoids parenting/lifetime
//   questions. Code-drawn icons pull their colours from the accessors
//   below so they track the active theme.
// ============================================================
namespace ThemeManager
{
enum class Theme { Dark, Light };

// ---- Persistence ----
// Load the saved theme + accent from QSettings ("ui/theme",
// "ui/accent"). Call once at startup before apply().
void load();

// Substitute the active palette into the :/theme.qss template, set the
// resulting stylesheet on the application, and install a matching
// QPalette. Safe to call repeatedly (live re-apply).
void apply(QApplication& app);

// ---- Setters (update state + persist + re-apply) ----
void setTheme(Theme theme);
void setAccent(const QColor& accent);

// ---- State accessors ----
Theme  theme();
bool   isDark();
QColor accent();

// ---- Colours for code-drawn icons (Icons::icon) ----
// Normal icon glyph colour — light grey on dark, dark grey on light.
QColor iconColor();
// Muted glyph colour (search adornment, spin arrows, …).
QColor iconMuted();
// Text/glyph colour drawn ON an accent fill (play, save) — ~white.
QColor onAccent();
// Destructive-action colour (delete button).
QColor danger();
}
