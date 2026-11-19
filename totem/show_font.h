#pragma once

// ============================================================================
// SHOW FONT — bold all-caps, 10 px tall. Pure, no Arduino: tested under tests/
// ============================================================================
//
// For the typed messages and the heart rate ticker (show_layout.h). Modelled
// on the heavy geometric sans Above and Beyond type on their screen: every
// stroke 2 px, counters 2 px, a pointed A, V, M and W. Each pixel is a 10 mm
// square behind the diffuser, so a letter stands 10 cm tall and 6 cm wide,
// and a face holds about two and a half of them.
//
// 'X' = lit. Every row of a glyph is the same width (checked by the tests).
// Lowercase is drawn as uppercase. A character not here is skipped.

#include <stdint.h>
#include <string.h>

namespace showfont {

const int H = 10;
const int GAP = 1;          // between letters

struct Glyph { char c; const char *rows[H]; };

// '#' is the ticker's heart.
static const Glyph FONT[] = {
  {' ', {"...", "...", "...", "...", "...", "...", "...", "...", "...", "..."}},
  {'A', {"..XXX..", "..XXX..", ".XXXXX.", ".XX.XX.", ".XX.XX.", "XX...XX", "XXXXXXX", "XXXXXXX", "XX...XX", "XX...XX"}},
  {'B', {"XXXXX.", "XXXXXX", "XX..XX", "XX..XX", "XXXXX.", "XXXXXX", "XX..XX", "XX..XX", "XXXXXX", "XXXXX."}},
  {'C', {".XXXXX", "XXXXXX", "XX....", "XX....", "XX....", "XX....", "XX....", "XX....", "XXXXXX", ".XXXXX"}},
  {'D', {"XXXXX.", "XXXXXX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XXXXXX", "XXXXX."}},
  {'E', {"XXXXX", "XXXXX", "XX...", "XX...", "XXXX.", "XXXX.", "XX...", "XX...", "XXXXX", "XXXXX"}},
  {'F', {"XXXXX", "XXXXX", "XX...", "XX...", "XXXX.", "XXXX.", "XX...", "XX...", "XX...", "XX..."}},
  {'G', {".XXXXX", "XXXXXX", "XX....", "XX....", "XX.XXX", "XX.XXX", "XX..XX", "XX..XX", "XXXXXX", ".XXXXX"}},
  {'H', {"XX..XX", "XX..XX", "XX..XX", "XX..XX", "XXXXXX", "XXXXXX", "XX..XX", "XX..XX", "XX..XX", "XX..XX"}},
  {'I', {"XX", "XX", "XX", "XX", "XX", "XX", "XX", "XX", "XX", "XX"}},
  {'J', {"....XX", "....XX", "....XX", "....XX", "....XX", "....XX", "XX..XX", "XX..XX", "XXXXXX", ".XXXX."}},
  {'K', {"XX..XX", "XX..XX", "XX.XX.", "XXXX..", "XXX...", "XXX...", "XXXX..", "XX.XX.", "XX..XX", "XX..XX"}},
  {'L', {"XX...", "XX...", "XX...", "XX...", "XX...", "XX...", "XX...", "XX...", "XXXXX", "XXXXX"}},
  {'M', {"XX....XX", "XXX..XXX", "XXXXXXXX", "XX.XX.XX", "XX.XX.XX", "XX....XX", "XX....XX", "XX....XX", "XX....XX", "XX....XX"}},
  {'N', {"XXX..XX", "XXX..XX", "XXXX.XX", "XXXX.XX", "XX.XXXX", "XX.XXXX", "XX..XXX", "XX..XXX", "XX...XX", "XX...XX"}},
  {'O', {"..XXX..", ".XXXXX.", "XX...XX", "XX...XX", "XX...XX", "XX...XX", "XX...XX", "XX...XX", ".XXXXX.", "..XXX.."}},
  {'P', {"XXXXX.", "XXXXXX", "XX..XX", "XX..XX", "XXXXXX", "XXXXX.", "XX....", "XX....", "XX....", "XX...."}},
  {'Q', {"..XXX..", ".XXXXX.", "XX...XX", "XX...XX", "XX...XX", "XX...XX", "XX.X.XX", "XX..XX.", ".XXXXXX", "..XXX.X"}},
  {'R', {"XXXXX.", "XXXXXX", "XX..XX", "XX..XX", "XXXXXX", "XXXXX.", "XX.XX.", "XX..XX", "XX..XX", "XX..XX"}},
  {'S', {".XXXXX", "XXXXXX", "XX....", "XX....", "XXXXX.", ".XXXXX", "....XX", "....XX", "XXXXXX", "XXXXX."}},
  {'T', {"XXXXXX", "XXXXXX", "..XX..", "..XX..", "..XX..", "..XX..", "..XX..", "..XX..", "..XX..", "..XX.."}},
  {'U', {"XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XXXXXX", ".XXXX."}},
  {'V', {"XX...XX", "XX...XX", "XX...XX", "XX...XX", ".XX.XX.", ".XX.XX.", ".XXXXX.", "..XXX..", "..XXX..", "...X..."}},
  {'W', {"XX....XX", "XX....XX", "XX....XX", "XX....XX", "XX....XX", "XX.XX.XX", "XX.XX.XX", "XXXXXXXX", "XXX..XXX", "XX....XX"}},
  {'X', {"XX..XX", "XX..XX", ".XXXX.", ".XXXX.", "..XX..", "..XX..", ".XXXX.", ".XXXX.", "XX..XX", "XX..XX"}},
  {'Y', {"XX..XX", "XX..XX", "XX..XX", "XXXXXX", ".XXXX.", "..XX..", "..XX..", "..XX..", "..XX..", "..XX.."}},
  {'Z', {"XXXXXX", "XXXXXX", "....XX", "...XX.", "..XX..", "..XX..", ".XX...", "XX....", "XXXXXX", "XXXXXX"}},
  {'0', {".XXXX.", "XXXXXX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XXXXXX", ".XXXX."}},
  {'1', {"..XX", ".XXX", "XXXX", "..XX", "..XX", "..XX", "..XX", "..XX", "..XX", "..XX"}},
  {'2', {".XXXX.", "XXXXXX", "XX..XX", "....XX", "...XX.", "..XX..", ".XX...", "XX....", "XXXXXX", "XXXXXX"}},
  {'3', {"XXXXX.", "XXXXXX", "....XX", "....XX", "..XXX.", "..XXX.", "....XX", "....XX", "XXXXXX", "XXXXX."}},
  {'4', {"...XX.", "..XXX.", ".XXXX.", "XX.XX.", "XX.XX.", "XXXXXX", "XXXXXX", "...XX.", "...XX.", "...XX."}},
  {'5', {"XXXXXX", "XXXXXX", "XX....", "XXXXX.", "XXXXXX", "....XX", "....XX", "XX..XX", "XXXXXX", ".XXXX."}},
  {'6', {".XXXX.", "XXXXX.", "XX....", "XXXXX.", "XXXXXX", "XX..XX", "XX..XX", "XX..XX", "XXXXXX", ".XXXX."}},
  {'7', {"XXXXXX", "XXXXXX", "....XX", "...XX.", "...XX.", "..XX..", "..XX..", ".XX...", ".XX...", ".XX..."}},
  {'8', {".XXXX.", "XXXXXX", "XX..XX", "XX..XX", ".XXXX.", "XXXXXX", "XX..XX", "XX..XX", "XXXXXX", ".XXXX."}},
  {'9', {".XXXX.", "XXXXXX", "XX..XX", "XX..XX", "XXXXXX", ".XXXXX", "....XX", "....XX", ".XXXXX", ".XXXX."}},
  {'-', {"....", "....", "....", "....", "XXXX", "XXXX", "....", "....", "....", "...."}},
  {'\'', {"XX", "XX", "XX", "..", "..", "..", "..", "..", "..", ".."}},
  {'!', {"XX", "XX", "XX", "XX", "XX", "XX", "..", "..", "XX", "XX"}},
  {'.', {"..", "..", "..", "..", "..", "..", "..", "..", "XX", "XX"}},
  {',', {"..", "..", "..", "..", "..", "..", "..", "XX", "XX", "X."}},
  {'?', {".XXXX.", "XXXXXX", "XX..XX", "....XX", "..XXX.", "..XX..", "..XX..", "......", "..XX..", "..XX.."}},
  {'&', {".XXX..", "XX.XX.", "XX.XX.", ".XXX..", ".XXX.X", "XX.XXX", "XX..XX", "XX.XXX", "XXXXXX", ".XXX.X"}},
  {'#', {".........", ".XXX.XXX.", "XXXXXXXXX", "XXXXXXXXX", "XXXXXXXXX", ".XXXXXXX.", "..XXXXX..", "...XXX...", "....X....", "........."}},
};
const int FONT_LEN = sizeof(FONT) / sizeof(FONT[0]);

inline char upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

// The glyph for c, or nullptr if the font doesn't have it. Callers uppercase
// text first (upper()).
inline const Glyph *find(char c) {
  for (int i = 0; i < FONT_LEN; i++)
    if (FONT[i].c == c) return &FONT[i];
  return nullptr;
}

inline int width(const Glyph *g) { return g ? (int)strlen(g->rows[0]) : 0; }

inline bool lit(const Glyph *g, int x, int y) {
  if (!g || y < 0 || y >= H || x < 0 || x >= width(g)) return false;
  return g->rows[y][x] == 'X';
}

} // namespace showfont
