#pragma once

#include <stdint.h>

// ============================================================================
// TOTEM CONFIG — the festival lantern: four 16x16 WS2812B grids, one per side
// ============================================================================
//
// Everything else (Whoop MAC, zone thresholds, brightness levels, wheel keys)
// still comes from the shared config.h, so a zone retune applies to the chest
// display and the totem alike.

// --- 1. LED data pin ---
// The WS2812B chain is driven from a pin on the board's HUB75 header, since the
// HUB75 panel is gone and the header's male pins take a female Dupont jumper
// with no soldering. GPIO 5 is R1, pin 1 of the 2x8 header (the corner marked
// with the triangle / square pad). GND is pin 4 (and 12, 14, 16 on most
// boards). Meter it before connecting the panels: pin 1 should idle near 0 V.
// UNVERIFIED on this board whether the header is buffered to 5 V logic; WS2812B
// panels normally accept 3.3 V data over a short lead. If the first panel
// flickers or shows random colours, the fix is a 74AHCT125 level shifter.
#define LED_PIN 5

// --- 2. Geometry ---
#define FACE_W 16
#define FACE_H 16
#define FACE_COUNT 4
#define LEDS_PER_FACE (FACE_W * FACE_H)
#define NUM_LEDS (LEDS_PER_FACE * FACE_COUNT)

// --- 2b. How each panel is wired, set once with TOTEM_ORIENTATION_TEST ---
// Most 16x16 panels are serpentine: row 0 runs one way, row 1 back the other.
// Which corner the data enters at depends on the brand and on how the panel
// sits in its frame, so these are a starting guess. Flash with the test on,
// then fix the flags until every face shows its number upright and the
// marker pixels sit where the test says. All four faces are mounted the same
// way round in the cube, so one set of flags covers them. (#ifndef only so the
// host tests can build every combination.)
#ifndef PANEL_SERPENTINE
#define PANEL_SERPENTINE 1   // 1: alternate rows reverse direction
#endif
#ifndef PANEL_FLIP_X
#define PANEL_FLIP_X     0   // 1: mirror left-right
#endif
#ifndef PANEL_FLIP_Y
#define PANEL_FLIP_Y     0   // 1: mirror top-bottom (data enters at the bottom)
#endif
#ifndef PANEL_TRANSPOSE
#define PANEL_TRANSPOSE  0   // 1: the panel's "rows" are columns (wired vertically)
#endif

// 1 = draw the orientation test instead of the heart rate. See totem.ino.
#define TOTEM_ORIENTATION_TEST 0

// --- 3. Power ---
// The LEDs get their own cable from the power bank's USB-A port (5 V, 3 A).
// USB-A, not the second USB-C port: a USB-C port only switches on for a device
// that identifies itself, and a bare-wire pigtail doesn't.
//
// FastLED scales every frame down to stay under this cap. At full scale the
// four faces would draw ~7.5 A, and ~2.6 A even at the wheel's middle level,
// so in practice this cap IS the brightness. 1800 mA keeps the whole totem
// near 9.5 W, which the Anker 537 (24,000 mAh) runs for about 6.5-7 hours
// even with the wheel turned all the way up. At the wheel's second level the
// draw is ~1.6 A and the bank lasts ~8 hours. Raise it for a brighter totem
// only if you can live with a shorter night.
#ifndef LED_MAX_MA
#define LED_MAX_MA 1800
#endif

// --- 4. Look ---
// Beat ripple: on each beat a faint ring in the zone colour spreads out from
// the heart across the dark pixels. 0 turns it off.
#define RIPPLE_ENABLED 1
#define RIPPLE_MS      450
