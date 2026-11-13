#pragma once

// ============================================================================
// LED MAP — (face, x, y) to position in the WS2812B chain. Pure; tested.
// ============================================================================
//
// Faces are chained in order 0..3 going round the cube, each 256 LEDs. x runs
// left to right as you look at the face from outside, y top to bottom. The
// PANEL_* flags in totem_config.h describe how the panel's own wiring differs
// from that; they apply in the order transpose, flip, serpentine.

#include "totem_config.h"

inline int ledIndex(int face, int x, int y) {
  int px = x, py = y;
#if PANEL_TRANSPOSE
  int t = px; px = py; py = t;
#endif
#if PANEL_FLIP_X
  px = FACE_W - 1 - px;
#endif
#if PANEL_FLIP_Y
  py = FACE_H - 1 - py;
#endif
#if PANEL_SERPENTINE
  if (py & 1) px = FACE_W - 1 - px;
#endif
  return face * LEDS_PER_FACE + py * FACE_W + px;
}
