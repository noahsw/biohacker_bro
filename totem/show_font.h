#pragma once

// ============================================================================
// SHOW FONTS — all-caps. Pure, no Arduino: tested under tests/
// ============================================================================
//
// Two fonts (show_layout.h):
//   BOLD   10 px tall, 2 px strokes: the heart rate ticker's heart and BPM.
//   LIGHT  7 px tall, 1 px strokes: the scrolling messages. The bold one read as
//          blobs behind the diffuser once whole words went past; a classic
//          5x7 keeps the counters open. Each pixel is a 10 mm square, so a
//          letter stands 7 cm tall and 5 cm wide.
//
// 'X' = lit. Every row of a glyph is the same width (checked by the tests).
// Lowercase is drawn as uppercase. A character not in a font is skipped.

#include <stdint.h>
#include <string.h>

namespace showfont {

const int MAX_H = 10;
const int GAP = 1;          // between letters

struct Glyph { char c; const char *rows[MAX_H]; };   // rows past the font's height are null
struct Font { const Glyph *glyphs; int count; int h; };

// '#' is the ticker's heart.
static const Glyph BOLD_GLYPHS[] = {
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

static const Glyph LIGHT_GLYPHS[] = {
  {' ', {"...", "...", "...", "...", "...", "...", "..."}},
  {'A', {".XXX.", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"}},
  {'B', {"XXXX.", "X...X", "X...X", "XXXX.", "X...X", "X...X", "XXXX."}},
  {'C', {".XXX.", "X...X", "X....", "X....", "X....", "X...X", ".XXX."}},
  {'D', {"XXXX.", "X...X", "X...X", "X...X", "X...X", "X...X", "XXXX."}},
  {'E', {"XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "XXXXX"}},
  {'F', {"XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "X...."}},
  {'G', {".XXX.", "X...X", "X....", "X.XXX", "X...X", "X...X", ".XXXX"}},
  {'H', {"X...X", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"}},
  {'I', {"XXX", ".X.", ".X.", ".X.", ".X.", ".X.", "XXX"}},
  {'J', {"..XXX", "...X.", "...X.", "...X.", "...X.", "X..X.", ".XX.."}},
  {'K', {"X...X", "X..X.", "X.X..", "XX...", "X.X..", "X..X.", "X...X"}},
  {'L', {"X....", "X....", "X....", "X....", "X....", "X....", "XXXXX"}},
  {'M', {"X...X", "XX.XX", "X.X.X", "X.X.X", "X...X", "X...X", "X...X"}},
  {'N', {"X...X", "X...X", "XX..X", "X.X.X", "X..XX", "X...X", "X...X"}},
  {'O', {".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."}},
  {'P', {"XXXX.", "X...X", "X...X", "XXXX.", "X....", "X....", "X...."}},
  {'Q', {".XXX.", "X...X", "X...X", "X...X", "X.X.X", "X..X.", ".XX.X"}},
  {'R', {"XXXX.", "X...X", "X...X", "XXXX.", "X.X..", "X..X.", "X...X"}},
  {'S', {".XXXX", "X....", "X....", ".XXX.", "....X", "....X", "XXXX."}},
  {'T', {"XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "..X.."}},
  {'U', {"X...X", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."}},
  {'V', {"X...X", "X...X", "X...X", "X...X", "X...X", ".X.X.", "..X.."}},
  {'W', {"X...X", "X...X", "X...X", "X.X.X", "X.X.X", "X.X.X", ".X.X."}},
  {'X', {"X...X", "X...X", ".X.X.", "..X..", ".X.X.", "X...X", "X...X"}},
  {'Y', {"X...X", "X...X", ".X.X.", "..X..", "..X..", "..X..", "..X.."}},
  {'Z', {"XXXXX", "....X", "...X.", "..X..", ".X...", "X....", "XXXXX"}},
  {'0', {".XXX.", "X...X", "X..XX", "X.X.X", "XX..X", "X...X", ".XXX."}},
  {'1', {"..X..", ".XX..", "..X..", "..X..", "..X..", "..X..", ".XXX."}},
  {'2', {".XXX.", "X...X", "....X", "...X.", "..X..", ".X...", "XXXXX"}},
  {'3', {"XXXXX", "...X.", "..X..", "...X.", "....X", "X...X", ".XXX."}},
  {'4', {"...X.", "..XX.", ".X.X.", "X..X.", "XXXXX", "...X.", "...X."}},
  {'5', {"XXXXX", "X....", "XXXX.", "....X", "....X", "X...X", ".XXX."}},
  {'6', {"..XX.", ".X...", "X....", "XXXX.", "X...X", "X...X", ".XXX."}},
  {'7', {"XXXXX", "....X", "...X.", "..X..", ".X...", ".X...", ".X..."}},
  {'8', {".XXX.", "X...X", "X...X", ".XXX.", "X...X", "X...X", ".XXX."}},
  {'9', {".XXX.", "X...X", "X...X", ".XXXX", "....X", "...X.", ".XX.."}},
  {'-', {"...", "...", "...", "XXX", "...", "...", "..."}},
  {'\'', {"X", "X", ".", ".", ".", ".", "."}},
  {'!', {"X", "X", "X", "X", "X", ".", "X"}},
  {'.', {".", ".", ".", ".", ".", ".", "X"}},
  {',', {"..", "..", "..", "..", "..", ".X", "X."}},
  {'?', {".XXX.", "X...X", "....X", "...X.", "..X..", ".....", "..X.."}},
  {'&', {".XX..", "X..X.", "X.X..", ".X...", "X.X.X", "X..X.", ".XX.X"}},
};

const Font BOLD = {BOLD_GLYPHS, (int)(sizeof(BOLD_GLYPHS) / sizeof(BOLD_GLYPHS[0])), 10};
const Font LIGHT = {LIGHT_GLYPHS, (int)(sizeof(LIGHT_GLYPHS) / sizeof(LIGHT_GLYPHS[0])), 7};

inline char upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

// The glyph for c, or nullptr if the font doesn't have it. Callers uppercase
// text first (upper()).
inline const Glyph *find(const Font &f, char c) {
  for (int i = 0; i < f.count; i++)
    if (f.glyphs[i].c == c) return &f.glyphs[i];
  return nullptr;
}

inline int width(const Glyph *g) { return g ? (int)strlen(g->rows[0]) : 0; }

inline bool lit(const Glyph *g, int x, int y) {
  if (!g || y < 0 || y >= MAX_H || !g->rows[y] || x < 0 || x >= width(g)) return false;
  return g->rows[y][x] == 'X';
}

} // namespace showfont
