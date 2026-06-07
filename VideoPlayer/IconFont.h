#pragma once

#include <QChar>
#include <QColor>
#include <QIcon>
#include <QString>

// ============================================================
// IconFont
//   Loads the embedded Material Icons font and renders individual
//   glyphs into QIcons (theme-coloured, hi-DPI aware). Keeps all
//   icon codepoints in one place so the UI never hard-codes magic
//   numbers or fragile emoji.
// ============================================================
namespace Icons
{
// ---- Material Icons codepoints (PUA) ----
inline constexpr char16_t PlayArrow    = 0xe037;
inline constexpr char16_t Pause        = 0xe034;
inline constexpr char16_t Stop         = 0xe047;
inline constexpr char16_t SkipPrevious = 0xe045;
inline constexpr char16_t SkipNext     = 0xe044;
inline constexpr char16_t Add          = 0xe145;
inline constexpr char16_t Remove       = 0xe15b;
inline constexpr char16_t FolderOpen   = 0xe2c8;
inline constexpr char16_t Delete       = 0xe872;
inline constexpr char16_t VolumeUp     = 0xe050;
inline constexpr char16_t VolumeDown   = 0xe04d;
inline constexpr char16_t VolumeOff    = 0xe04f;
inline constexpr char16_t Repeat       = 0xe040;
inline constexpr char16_t RepeatOne    = 0xe041;
inline constexpr char16_t PhotoCamera  = 0xe412;
inline constexpr char16_t Image        = 0xe3f4;
inline constexpr char16_t AutoFixHigh  = 0xe663;
inline constexpr char16_t Save         = 0xe161;
inline constexpr char16_t Search       = 0xe8b6;
inline constexpr char16_t Star         = 0xe838;
inline constexpr char16_t Tune         = 0xe429;
inline constexpr char16_t Hd           = 0xe052;
inline constexpr char16_t Fullscreen   = 0xe5d0;

// Load the embedded font once at startup. Returns the resolved family
// name (empty on failure). Safe to call multiple times.
QString loadFont();

// Resolved Material Icons family name (valid after loadFont()).
QString family();

// Render a glyph to a QIcon at the given logical pixel size + colour.
// Honours the application device-pixel-ratio so icons stay crisp on
// high-DPI displays.
QIcon icon(char16_t codepoint, const QColor& color, int px = 22);
}
