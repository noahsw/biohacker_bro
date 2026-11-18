#pragma once

// ============================================================================
// FACE LAYOUT (16x16) — pure, no Arduino, no FastLED: tested under tests/
// ============================================================================
//
//   row  0       the zone bar: the chest display's legend, dim, lit in its own
//                colours up to the current BPM (slices 3,3,3,3,4 px wide)
//   row  1       gap, so the bar and the heart's lobes read as two things
//   rows 2..7    the heart, 8x6 (cols 4..11), red, pulsing with each beat
//   row  8       gap
//   rows 9..15   BPM, 5x7 digits, zone-coloured, centred
//
// Each pixel is a 10 mm square behind a white diffuser, so the digits are
// 70 mm tall and the heart 60 mm. Steps are gone: from across a field, the
// heart, the number and the bar are what read.
//
// The digits were 4x6 under a 10x7 heart. On the panels they read thin, and
// 5-wide digits looked impossible: three of them with gaps make 17 px. But a
// three-digit heart rate always starts with a 1, so the hundreds digit is a
// 3-wide 1 and the number is 3+1+5+1+5 = 15 px. The heart gave up a row to
// make the digits a row taller.
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
const int HEART_X = 4, HEART_Y = 2, HEART_W = 8, HEART_H = 6;
const int DIGIT_Y = 9, DIGIT_W = 5, DIGIT_H = 7, DIGIT_GAP = 1;
const int NARROW_W = 3;   // the hundreds 1

static const char *const HEART[HEART_H] = {
  ".XX..XX.",
  "XXXXXXXX",
  "XXXXXXXX",
  ".XXXXXX.",
  "..XXXX..",
  "...XX...",
};

// 5x7. Index 0-9 are digits, 10 is '-', 11 is the narrow hundreds 1 (only
// its first NARROW_W columns are used). The 1 at index 1 keeps a full 5-wide
// cell, so a two-digit number doesn't shift sideways when a 1 turns up.
const int GLYPH_DASH = 10, GLYPH_NARROW_ONE = 11;
static const char *const GLYPH[12][DIGIT_H] = {
  {".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."},
  {"..X..", ".XX..", "..X..", "..X..", "..X..", "..X..", ".XXX."},
  {".XXX.", "X...X", "....X", "...X.", "..X..", ".X...", "XXXXX"},
  {"XXXX.", "....X", "....X", ".XXX.", "....X", "....X", "XXXX."},
  {"...X.", "..XX.", ".X.X.", "X..X.", "XXXXX", "...X.", "...X."},
  {"XXXXX", "X....", "XXXX.", "....X", "....X", "X...X", ".XXX."},
  {".XXX.", "X....", "X....", "XXXX.", "X...X", "X...X", ".XXX."},
  {"XXXXX", "....X", "...X.", "..X..", ".X...", ".X...", ".X..."},
  {".XXX.", "X...X", "X...X", ".XXX.", "X...X", "X...X", ".XXX."},
  {".XXX.", "X...X", "X...X", ".XXXX", "....X", "....X", ".XXX."},
  {".....", ".....", ".....", "XXXXX", ".....", ".....", "....."},
  {".X...", "XX...", ".X...", ".X...", ".X...", ".X...", "XXX.."},
};

inline int glyphWidth(int g) { return g == GLYPH_NARROW_ONE ? NARROW_W : DIGIT_W; }

// Heart centre, for the beat ripple.
const float HEART_CX = HEART_X + HEART_W / 2.0f - 0.5f;   // 7.5
const float HEART_CY = HEART_Y + 2.5f;

inline bool heartAt(int x, int y) {
  int c = x - HEART_X, r = y - HEART_Y;
  return c >= 0 && c < HEART_W && r >= 0 && r < HEART_H && HEART[r][c] == 'X';
}

// Outline of the heart: lit pixels with an unlit 4-neighbour (or the edge).
inline bool heartEdgeAt(int x, int y) {
  if (!heartAt(x, y)) return false;
  return !heartAt(x + 1, y) || !heartAt(x - 1, y) || !heartAt(x, y + 1) || !heartAt(x, y - 1);
}

// The zone bar's fill in whole pixels, from its exact value (0..16) and the
// fill shown last frame. Plain rounding made a BPM wobbling by one across a
// rounding point (62/63 on the default zones: 1.49 vs 1.71 px) flick the end
// pixel on and off. So a one-pixel step waits until the value is
// BAR_HOLD_PX past that point. Bigger jumps go straight through.
const float BAR_HOLD_PX = 0.25f;
inline int barFill(float exact, int prev) {
  int r = (int)(exact + 0.5f);
  if (r != prev + 1 && r != prev - 1) return r;
  float d = exact - ((r > prev ? r : prev) - 0.5f);   // distance from the rounding point
  if (d < 0) d = -d;
  return d < BAR_HOLD_PX ? prev : r;
}

// The smallest channel value that's still lit after FastLED's global
// brightness `b`. With dithering off, FastLED scales each channel as
// v * (b + 1) >> 8, so anything under 256 / (b + 1) rounds to 0. That made
// the bar's dim legend go blank: zone 0's unlit grey is ~10, and at the
// capped brightness of ~14 that comes out as 0.
inline uint8_t visibleFloor(uint8_t b) { return (uint8_t)((256 + b) / (b + 1)); }   // ceil(256 / (b+1))

// Width of a run of glyphs, and the left column that centres it on 16.
// Odd widths (2 digits = 11, 3 = 15) round right, so "72" sits under the
// heart's centre line rather than a pixel left of it.
inline int textWidth(const int *glyphs, int n) {
  int w = (n - 1) * DIGIT_GAP;
  for (int i = 0; i < n; i++) w += glyphWidth(glyphs[i]);
  return w;
}
inline int textX(const int *glyphs, int n) {
  int w = textWidth(glyphs, n);
  return (W - w + (w % 2)) / 2;
}

// Writes the glyph indices of `bpm` into out (max 3), returns the count.
// Below 10 or above 199 can't come from a heart, so they show as "--".
// (200+ would also need a 5-wide hundreds digit, and that doesn't fit.)
inline int bpmGlyphs(int bpm, int out[3]) {
  if (bpm < 10 || bpm > 199) { out[0] = out[1] = GLYPH_DASH; return 2; }
  if (bpm >= 100) { out[0] = GLYPH_NARROW_ONE; out[1] = bpm / 10 % 10; out[2] = bpm % 10; return 3; }
  out[0] = bpm / 10; out[1] = bpm % 10;
  return 2;
}

// True where the BPM text lights a pixel.
inline bool textAt(const int *glyphs, int n, int x, int y) {
  int r = y - DIGIT_Y;
  if (r < 0 || r >= DIGIT_H) return false;
  int x0 = textX(glyphs, n);
  for (int i = 0; i < n; i++) {
    int gw = glyphWidth(glyphs[i]);
    int c = x - x0;
    if (c >= 0 && c < gw) return GLYPH[glyphs[i]][r][c] == 'X';
    x0 += gw + DIGIT_GAP;
  }
  return false;
}

} // namespace face
