#pragma once

// ============================================================================
// FACE LAYOUT (16x16) — pure, no Arduino, no FastLED: tested under tests/
// ============================================================================
//
//   row  0       the zone bar: the chest display's legend, dim, lit in its own
//                colours up to the current BPM (slices 3,3,3,3,4 px wide)
//   row  1       gap, so the bar and the heart's lobes read as two things
//   rows 2..7    the heart, 8x6 (cols 4..11), hot pink, pulsing with each beat
//   row  8       gap
//   rows 9..15   BPM, 5x7 digits, zone-coloured, centred
//
// Each pixel is a 10 mm square behind a white diffuser, so the digits are
// 70 mm tall and the heart 60 mm. Steps are gone: from across a field, the
// heart, the number and the bar are what read.
//
// The digits were 4x6 under a 10x7 heart. On the panels they read thin, and
// 5-wide digits looked impossible: three of them with gaps make 17 px. But a
// three-digit heart rate always starts with a 1, so the hundreds digit can be
// narrow. The heart gave up a row to make the digits a row taller.
//
// The hundreds 1 is two columns, flag and stem: 2+1+5+1+5 = 14 px. With four
// faces side by side, the gap between two numbers is 16 minus the width,
// whatever the centring. A 3-wide 1 (with feet) made it 15 px, so the gap
// between panels was 1 px, the same as between digits, and "145 162" read as
// "14516 2". At 14 px it's 2 px, twice the gap between digits.
//
// Every 1 is that same flag-and-stem shape, with no feet: a bare-stroke
// hundreds 1 next to a footed ones 1 (as in "131") looked like two fonts.
//
// In a three-digit number, every 1 is the narrow one, not just the hundreds.
// A 1 in a full 5-wide cell sits with 2 blank columns on its right, so "111"
// put its stems 4 then 6 px apart. Narrow 1s space evenly, 1 px apart like
// every other digit. The cost: a three-digit number is 14, 11 or 8 px wide
// depending on its 1s, so it re-centres (by 1-3 px) when one comes or goes.
// Two-digit numbers keep the full-cell 1 and never move.
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
const int NARROW_W = 2;   // the hundreds 1: flag and stem, no padding

static const char *const HEART[HEART_H] = {
  ".XX..XX.",
  "XXXXXXXX",
  "XXXXXXXX",
  ".XXXXXX.",
  "..XXXX..",
  "...XX...",
};

// 5x7. Index 0-9 are digits, 10 is '-', 11 is the narrow 1 (only
// its first NARROW_W columns are used). The 1 at index 1 keeps a full 5-wide
// cell, so a two-digit number doesn't shift sideways when a 1 turns up.
const int GLYPH_DASH = 10, GLYPH_NARROW_ONE = 11;
static const char *const GLYPH[12][DIGIT_H] = {
  {".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."},
  {"..X..", ".XX..", "..X..", "..X..", "..X..", "..X..", "..X.."},
  {".XXX.", "X...X", "....X", "...X.", "..X..", ".X...", "XXXXX"},
  {"XXXX.", "....X", "....X", ".XXX.", "....X", "....X", "XXXX."},
  {"...X.", "..XX.", ".X.X.", "X..X.", "XXXXX", "...X.", "...X."},
  {"XXXXX", "X....", "XXXX.", "....X", "....X", "X...X", ".XXX."},
  {".XXX.", "X....", "X....", "XXXX.", "X...X", "X...X", ".XXX."},
  {"XXXXX", "....X", "...X.", "..X..", ".X...", ".X...", ".X..."},
  {".XXX.", "X...X", "X...X", ".XXX.", "X...X", "X...X", ".XXX."},
  {".XXX.", "X...X", "X...X", ".XXXX", "....X", "....X", ".XXX."},
  {".....", ".....", ".....", "XXXXX", ".....", ".....", "....."},
  {".X...", "XX...", ".X...", ".X...", ".X...", ".X...", ".X..."},
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
// rounding point (e.g. 1.49 vs 1.71 px) flick the end
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
// the bar's dim legend go blank: an unlit legend pixel can be ~10, and at the
// capped brightness of ~14 that comes out as 0.
inline uint8_t visibleFloor(uint8_t b) { return (uint8_t)((256 + b) / (b + 1)); }   // ceil(256 / (b+1))

// What FastLED does to one channel: nscale8_video(level), then the global
// brightness b with dithering off.
inline uint8_t heartChannelOut(uint8_t v, uint8_t s, uint8_t b) {
  uint8_t pre = (uint8_t)(((uint16_t)v * s >> 8) + (v && s ? 1 : 0));   // nscale8_video
  return (uint8_t)((uint16_t)pre * (b + 1) >> 8);
}

// The heart's lowest beat level at global brightness b. It rests at `base`
// between beats, but at the bottom brightness level (b = 18) the hot pink's
// resting (46, 5, 20) comes out as (3, 0, 1): its blue nearly gone, so it
// read as red. So any channel that carries the colour (a quarter of the
// brightest or more; the pink's green doesn't) must come out at 2 or more,
// and the rest level rises just enough for that. Higher brightness levels
// already clear it, so they keep `base` and the full depth of the pulse. If
// even full level can't reach 2 the floor would stop the pulse, so it stays
// at base.
inline float heartMinLevel(Rgb c, uint8_t b, float base) {
  uint8_t mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
  const uint8_t ch[3] = {c.r, c.g, c.b};
  int s = (int)(255 * base);
  for (int i = 0; i < 3; i++) {
    if (ch[i] == 0 || ch[i] * 4 < mx) continue;
    while (s < 255 && heartChannelOut(ch[i], (uint8_t)s, b) < 2) s++;
    if (heartChannelOut(ch[i], (uint8_t)s, b) < 2) return base;
  }
  float l = s / 255.0f;
  return l > base ? l : base;
}

// Per person on the faces, so you and your friend each keep your own: the
// zone bar's fill, and the number showing and the one it's fading from
// (BPM_FADE_MS in totem_config.h).
struct FaceState {
  int barFill = 0;
  int glyphs[3] = {0, 0, 0}, n = 0;
  Rgb color = {0, 0, 0};
  int oldGlyphs[3] = {0, 0, 0}, oldN = 0;
  Rgb oldColor = {0, 0, 0};
  unsigned long changedAt = 0;
};

// Width of a run of glyphs, and the left column that centres it on 16.
// Odd widths (2 digits = 11, 3 = 13) round right, so "72" sits under the
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
// Every 1 in a three-digit number is the narrow one (see the header).
// (200+ would also need a 5-wide hundreds digit, and that doesn't fit.)
inline int bpmGlyphs(int bpm, int out[3]) {
  if (bpm < 10 || bpm > 199) { out[0] = out[1] = GLYPH_DASH; return 2; }
  if (bpm >= 100) {
    out[0] = GLYPH_NARROW_ONE; out[1] = bpm / 10 % 10; out[2] = bpm % 10;
    for (int i = 1; i < 3; i++) if (out[i] == 1) out[i] = GLYPH_NARROW_ONE;
    return 3;
  }
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
