#pragma once

#include <stdint.h>

// ============================================================================
// TOTEM CONFIG — the festival lantern
// ============================================================================
//
// Two layouts share this sketch. FACES (the default): four 16x16 WS2812B
// panels wrapped round the head, every panel showing the same frame at the
// same time: the zone bar, a heart pulsing on your beats, and your BPM.
// RING (TOTEM_RING 1, kept for later): three panels as one 48x16 ring with an
// EKG trace and a ticker of your crew's heart rates.
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
// 0 = four panels, all showing the same face (the build). 1 = 3-panel EKG ring.
#ifndef TOTEM_RING
#define TOTEM_RING 0
#endif
#define FACE_W 16
#define FACE_H 16
#if TOTEM_RING
#define FACE_COUNT 3
#else
#define FACE_COUNT 4
#endif
#define RING_COLS (FACE_W * FACE_COUNT)
#define LEDS_PER_FACE (FACE_W * FACE_H)
#define NUM_LEDS (LEDS_PER_FACE * FACE_COUNT)

// --- 2b. How each panel is wired, set once with TOTEM_ORIENTATION_TEST ---
// Most 16x16 panels are serpentine: row 0 runs one way, row 1 back the other.
// Which corner the data enters at depends on the brand and on how the panel
// sits in its frame, so these are a starting guess. Flash with the test on,
// then fix the flags until every face shows its number upright and the
// marker pixels sit where the test says. All panels are mounted the same
// way round, so one set of flags covers them. (#ifndef only so the host tests
// can build every combination.)
//
// Ring only: the test also runs a white dot along row 15 from column 0 to 47.
// If it jumps backwards at a seam between panels, the chain runs round the
// ring the other way: set RING_REVERSE to 1.
#ifndef PANEL_SERPENTINE
#define PANEL_SERPENTINE 1   // 1: alternate rows reverse direction
#endif
#ifndef PANEL_FLIP_X
#define PANEL_FLIP_X     0   // 1: mirror left-right
#endif
#ifndef PANEL_FLIP_Y
#define PANEL_FLIP_Y     1   // 1: mirror top-bottom (data enters at the bottom)
#endif
#ifndef PANEL_TRANSPOSE
#define PANEL_TRANSPOSE  0   // 1: the panel's "rows" are columns (wired vertically)
#endif

#ifndef RING_REVERSE
#define RING_REVERSE     0   // 1: panel 0 is on the right of panel 1, not the left
#endif

// 1 = draw the orientation test instead of the heart rate. See totem.ino.
#define TOTEM_ORIENTATION_TEST 0

// --- 3. Power ---
// The LEDs get their own cable from the power bank's USB-A port (5 V, 3 A).
// USB-A, not the second USB-C port: a USB-C port only switches on for a device
// that identifies itself, and a bare-wire pigtail doesn't.
//
// FastLED scales every frame down to stay under this cap. 1800 mA keeps the
// whole totem near 9.5 W, which the Anker 537 (24,000 mAh) runs for about
// 6.5-7 hours even if every frame hits the cap.
//
// By FastLED's own estimate (which counts ~1 mA for every unlit LED, so it
// reads high) the ring averages ~2.1 A at the wheel's middle level, ~1.5 A
// at level 1 and ~1.3 A at the lowest. So from the middle level up the cap is
// what sets the brightness and the night is ~6.5-7 h; at level 1 it's ~8 h.
// Raise the cap for a brighter totem only if you can live with a shorter night.
#ifndef LED_MAX_MA
#define LED_MAX_MA 1800
#endif

// Cap used instead while a computer is on the board's USB-C port, so the
// panels can run off the Mac at the desk with the battery left out. The
// firmware tells the two apart because a computer talks USB (it sends a
// start-of-frame packet every millisecond) and a power bank never does. With
// both the Mac and the battery plugged in it uses this lower cap: the safe
// side. Unplug the Mac and the full LED_MAX_MA is back within ~2 s.
#ifndef LED_MAX_MA_USB_HOST
#define LED_MAX_MA_USB_HOST 500
#endif

// --- 4. Look ---
// Beat ripple: on each beat a faint ring in the zone colour spreads out from
// the heart across the dark pixels. Off: on the real panels it was too much
// on top of the pulsing heart. 1 brings it back.
#define RIPPLE_ENABLED 0
#define RIPPLE_MS      450

// The bar's current-position pixel blinks: this long on, this long at the
// unlit level, so you can find where you are on the bar at a glance.
#define BAR_BLINK_MS   500

// --- 5. Round totem: EKG and ticker ---
#define EKG_PX_PER_S     30    // write head speed: once round the 48 columns in 1.6 s
#define EKG_FADE_MS      900   // how fast the trace behind the head dims
#define TICKER_PX_PER_S  8     // ticker drift; a 3-digit rate passes a viewer in ~2 s

// --- 6. Crew: friends' heart rates on the ring ---
// Up to four friends' Whoops (HR Broadcast on) or any standard Bluetooth heart
// rate strap. Each friend shows as their initial and BPM in the ticker while
// connected, and drops out when they walk off. Get a strap's MAC with the
// find_whoop_mac sketch, the same way you got yours.
//
// Put the MACs in secrets.h (#define CREW_MAC_1 "AA:BB:...") or here. "" = empty.
//
// The BLE library allows 3 connections by default: you + 2 friends. For 3 or
// 4 friends, open NimBLE-Arduino's src/nimconfig.h and change the commented
//   // #define MYNEWT_VAL_BLE_MAX_CONNECTIONS 3
// to
//   #define MYNEWT_VAL_BLE_MAX_CONNECTIONS 5
// (the ESP32 radio itself allows 9). Without that, friends past the second
// just never appear, and the serial log says why.
//
// Read secrets.h now, so its CREW_MAC_* win over the "" defaults below
// whichever header a file happens to include first.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef CREW_ENABLED
#define CREW_ENABLED 1
#endif
#ifndef CREW_MAC_1
#define CREW_MAC_1 ""
#endif
#ifndef CREW_MAC_2
#define CREW_MAC_2 ""
#endif
#ifndef CREW_MAC_3
#define CREW_MAC_3 ""
#endif
#ifndef CREW_MAC_4
#define CREW_MAC_4 ""
#endif
#ifndef CREW_INITIAL_1
#define CREW_INITIAL_1 'A'
#endif
#ifndef CREW_INITIAL_2
#define CREW_INITIAL_2 'B'
#endif
#ifndef CREW_INITIAL_3
#define CREW_INITIAL_3 'C'
#endif
#ifndef CREW_INITIAL_4
#define CREW_INITIAL_4 'D'
#endif
#define CREW_MAX 4

// --- Friend on the faces (FACES layout) ---
// 1: when a friend's strap is connected, two opposite faces show them with a
// cyan heart (everything else as for you), the other two show you with a red heart. 0: all four show you.
// Only the first connected friend is shown; the ring layout shows them all.
// Either way, if your Whoop drops out while a friend is live, all four faces
// show that friend.
#ifndef CREW_FACES
#define CREW_FACES 1
#endif
#define HEART_RGB_YOU    CRGB(255, 20, 20)
#define HEART_RGB_FRIEND CRGB(0, 200, 255)
