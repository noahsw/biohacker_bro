#pragma once

// ============================================================================
// MUSIC ON THE FACES — pure, no Arduino, no FastLED: tested under tests/
// ============================================================================
//
// The music's BPM gets a readout of its own, built like a heart rate one so
// it reads as the same kind of thing, but plainly not a heart:
//
//   row  0       a beat counter: 1-2-3-4, four pixels more on each beat
//   row  1       gap
//   rows 2..7    a music note (or a speaker, MUSIC_ICON 1), 8x6, in the
//                music's colour, pulsing on the song's beats
//   row  8       gap
//   rows 9..15   the BPM, 5x7 digits (face_layout.h), in the music's colour
//
// Which readouts go round the ring (who's live decides):
//
//   you, a friend and the music   three readouts, evenly spaced (21 columns
//                                 apart, so 5 dark columns between them),
//                                 drifting slowly round so every side of
//                                 the crowd sees all three
//   any two of them               the two on alternate panels, as the
//                                 friend layout has always been: you on
//                                 two opposite panels, the other on the
//                                 other two, still
//   just one                      that one on all four panels
//   nobody (no strap, no music)   your heart, searching, on all four
//
// Your own heart rate dropping out (strap off) takes you out of the list, so
// "your friend and the music" is two readouts, and "just the music" is one.

#include "face_layout.h"

#include <math.h>
#include <stdint.h>

namespace music {

const int ICON_X = face::HEART_X, ICON_Y = face::HEART_Y;   // the heart's box
const int ICON_W = face::HEART_W, ICON_H = face::HEART_H;

// A pair of beamed eighth notes, as on sheet music: two stems joined at the
// top by a thick beam, a note head at the foot of each.
static const char *const NOTE[ICON_H] = {
  "..XXXXXX",
  "..XXXXXX",
  "..X....X",
  "..X....X",
  "XXX..XXX",
  "XXX..XXX",
};

// A speaker cone with two sound waves.
static const char *const SPEAKER[ICON_H] = {
  "...X.X..",
  "..XX..X.",
  "XXXX.X.X",
  "XXXX.X.X",
  "..XX..X.",
  "...X.X..",
};

inline bool iconAt(int icon, int x, int y) {
  int c = x - ICON_X, r = y - ICON_Y;
  if (c < 0 || c >= ICON_W || r < 0 || r >= ICON_H) return false;
  return (icon == 1 ? SPEAKER : NOTE)[r][c] == 'X';
}

// The beat counter's lit pixels after `beats` beats: 4, 8, 12, 16, 4, ...
inline int beatBarFill(uint32_t beats) { return 4 * (int)(beats % 4 + 1); }

// ---------------------------------------------------------------------------
// Who goes where
// ---------------------------------------------------------------------------

enum Kind : uint8_t { YOU, FRIEND, MUSIC };

enum Mode : uint8_t {
  ALL,    // one readout on all four panels
  PAIR,   // two: the first on even panels, the second on odd
  TRIO,   // three, evenly spaced, drifting round the ring
};

struct Layout {
  Mode mode = ALL;
  int n = 1;
  Kind k[3] = {YOU, YOU, YOU};
  bool operator==(const Layout &o) const {
    if (mode != o.mode || n != o.n) return false;
    for (int i = 0; i < n; i++) if (k[i] != o.k[i]) return false;
    return true;
  }
  bool operator!=(const Layout &o) const { return !(*this == o); }
};

// youLive: your strap is connected and sending. friendLive: a friend's is
// (and crewFaces allows friends on the faces; with you gone a friend shows
// anyway, so the totem isn't dark while you step away). musicLive: the tempo
// is locked. With nothing live it's you, searching, on all four.
inline Layout pick(bool youLive, bool friendLive, bool musicLive, bool crewFaces) {
  Layout L;
  L.n = 0;
  if (youLive) L.k[L.n++] = YOU;
  if (friendLive && (crewFaces || !youLive)) L.k[L.n++] = FRIEND;
  if (musicLive) L.k[L.n++] = MUSIC;
  if (L.n == 0) { L.n = 1; L.k[0] = YOU; }
  L.mode = L.n == 1 ? ALL : L.n == 2 ? PAIR : TRIO;
  // Round the ring: you, the music, your friend. In a trio each is next to
  // both others anyway; this keeps the two heart rates from reading as a pair.
  if (L.mode == TRIO) { L.k[0] = YOU; L.k[1] = MUSIC; L.k[2] = FRIEND; }
  return L;
}

// ---------------------------------------------------------------------------
// The trio's drift
// ---------------------------------------------------------------------------

// For ring column col (0..cols-1) with the trio turned by off columns: which
// readout covers it (-1 for the dark gap between two), and where in that
// readout's 16 columns: column u0 and the one right of it, mixed by frac
// (0 = all u0). u0 can be -1 or 15: the readout's edge sliding in, with the
// missing neighbour dark. That in-between mix is what makes the drift glide
// rather than tick along a column at a time.
inline int trioAt(float off, int col, int cols, int &u0, float &frac) {
  const float pitch = cols / 3.0f;
  for (int t = 0; t < 3; t++) {
    float left = off + t * pitch;
    float u = fmodf(col - left, (float)cols);
    if (u < 0) u += cols;
    if (u > cols - 1) u -= cols;   // just left of the readout's edge
    if (u >= -1.0f && u < face::W) {
      float f = floorf(u);
      u0 = (int)f;
      frac = u - f;
      return t;
    }
  }
  u0 = 0; frac = 0;
  return -1;
}

// The trio's turn at time ms, drifting pxPerS columns a second, wrapped to
// one lap.
inline float trioOffset(uint32_t ms, float pxPerS, int cols) {
  float o = fmodf(ms / 1000.0f * pxPerS, (float)cols);
  return o < 0 ? o + cols : o;
}

} // namespace music
