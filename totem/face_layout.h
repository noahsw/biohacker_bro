#pragma once

// ============================================================================
// FACE LAYOUT (16x16) — pure, no Arduino, no FastLED: tested under tests/
// ============================================================================
//
//   row  0       the zone bar: the chest display's legend, dim, lit in its own
//                colours up to the current BPM (slices 3,3,3,3,4 px wide)
//   row  1       gap, so the bar and the heart's lobes read as two things
//   rows 2..8    the heart, 10 wide (cols 3..12), red, pulsing with each beat
//   row  9       gap
//   rows 10..15  BPM, 4x6 digits, zone-coloured, centred
//
// Each pixel is a 10 mm square behind a white diffuser, so the digits are
// 60 mm tall and the heart 70 mm. Steps are gone: from across a field, the
// heart, the number and the bar are what read.
//
// The bar is on TOP, not the bottom, because a bottom bar would sit against
// the digits in the same colour and fuse with them. On top, a one-pixel gap
// keeps it off the heart's lobes.
//
// Mirrors 3d-prints/edm-totem/source/face_layout.py, which drew the renders.

#include <stdint.h>
#include <string.h>

struct Rgb { uint8_t r, g, b; };

namespace face {

const int W = 16, H = 16;
const int BAR_Y = 0;
const int HEART_X = 3, HEART_Y = 2, HEART_W = 10, HEART_H = 7;
const int DIGIT_Y = 10, DIGIT_W = 4, DIGIT_H = 6, DIGIT_GAP = 1;

static const char *const HEART[HEART_H] = {
  ".XXX..XXX.",
  "XXXXXXXXXX",
  "XXXXXXXXXX",
  ".XXXXXXXX.",
  "..XXXXXX..",
  "...XXXX...",
  "....XX....",
};

// 4x6. Index 0-9 are digits, 10 is '-'.
static const char *const GLYPH[11][DIGIT_H] = {
  {".XX.", "X..X", "X..X", "X..X", "X..X", ".XX."},
  {"..X.", ".XX.", "..X.", "..X.", "..X.", ".XXX"},
  {".XX.", "X..X", "...X", "..X.", ".X..", "XXXX"},
  {"XXX.", "...X", ".XX.", "...X", "...X", "XXX."},
  {"X..X", "X..X", "XXXX", "...X", "...X", "...X"},
  {"XXXX", "X...", "XXX.", "...X", "...X", "XXX."},
  {".XX.", "X...", "XXX.", "X..X", "X..X", ".XX."},
  {"XXXX", "...X", "..X.", "..X.", ".X..", ".X.."},
  {".XX.", "X..X", ".XX.", "X..X", "X..X", ".XX."},
  {".XX.", "X..X", "X..X", ".XXX", "...X", ".XX."},
  {"....", "....", "XXXX", "....", "....", "...."},
};

// Heart centre, for the beat ripple.
const float HEART_CX = HEART_X + HEART_W / 2.0f - 0.5f;   // 7.5
const float HEART_CY = HEART_Y + 3.0f;

inline bool heartAt(int x, int y) {
  int c = x - HEART_X, r = y - HEART_Y;
  return c >= 0 && c < HEART_W && r >= 0 && r < HEART_H && HEART[r][c] == 'X';
}

// Outline of the heart: lit pixels with an unlit 4-neighbour (or the edge).
inline bool heartEdgeAt(int x, int y) {
  if (!heartAt(x, y)) return false;
  return !heartAt(x + 1, y) || !heartAt(x - 1, y) || !heartAt(x, y + 1) || !heartAt(x, y - 1);
}

// Width of a run of glyphs, and the left column that centres it on 16.
// Odd widths (2 digits = 9) round right, so "72" sits under the heart's
// centre line rather than a pixel left of it.
inline int textWidth(int n) { return n * DIGIT_W + (n - 1) * DIGIT_GAP; }
inline int textX(int n) {
  int w = textWidth(n);
  return (W - w + (w % 2)) / 2;
}

// Writes the glyph indices of `bpm` into out (max 3), returns the count.
// Below 10 or above 999 can't come from a heart, so they show as "--".
inline int bpmGlyphs(int bpm, int out[3]) {
  if (bpm < 10 || bpm > 999) { out[0] = out[1] = 10; return 2; }
  int n = bpm >= 100 ? 3 : 2;
  for (int i = n - 1; i >= 0; i--) { out[i] = bpm % 10; bpm /= 10; }
  return n;
}

// True where the BPM text lights a pixel.
inline bool textAt(const int *glyphs, int n, int x, int y) {
  int r = y - DIGIT_Y;
  if (r < 0 || r >= DIGIT_H) return false;
  int x0 = textX(n);
  for (int i = 0; i < n; i++) {
    int c = x - (x0 + i * (DIGIT_W + DIGIT_GAP));
    if (c >= 0 && c < DIGIT_W) return GLYPH[glyphs[i]][r][c] == 'X';
  }
  return false;
}

} // namespace face
