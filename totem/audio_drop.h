#pragma once

#include <Arduino.h>
#include <FastLED.h>

// ============================================================================
// BEAT DROP: THE BOARD'S MICS, AND THE BURST ON A DROP
// ============================================================================
//
// The board's two mics feed its ES7210 audio chip, which streams to the ESP32
// over I2S. A task on the other core listens to the bass and spots drops
// (drop_detect.h). On a drop, while the heart rate is showing, a white ring
// spreads out from the heart on every face (drawn by the face code, timed by
// dropRingAgeMs()); a drop during a show or with the lights off is ignored.
// DROP_ANIM 1 swaps the ring for the rainbow burst (drop_fx.h), and
// DROP_TEST_MODE swaps the heart rate for a sound meter.
//
// DROP_ENABLED 0 (totem_config.h) means off: the mics aren't powered, no I2S,
// no task, and nothing on the faces changes.
//
// Wiring, from the board maker's schematic (Seengreat RGB Matrix HUB75 S3):
//   ES7210 at I2C 0x40 on IO1/IO2 (the same bus as the wheel's PCA9557)
//   I2S: MCLK IO38, BCLK IO48, LRCK IO21, ES7210 SDOUT1 -> IO47
//   The audio chips' 3.3 V comes from a regulator switched by the PCA9557's
//   IO5 ("Audio_PWR_EN"), which is off at power-up, so it's switched on here.
//   (The ES8311 at 0x18 drives the speaker output; it isn't used.)

// From setup(), after Wire.begin() and brightnessSetup(). Starts the mics if
// DROP_ENABLED or DROP_TEST_MODE is set; otherwise does nothing at all.
void dropSetup();

// Turn drop detection on or off while running (for the phone remote later).
// The first time it's turned on, this powers up and starts the mics, so call
// it from loop(), not from a Bluetooth callback. Off stops the bursts at once;
// the mics keep listening in case it comes back on.
void dropSetEnabled(bool on);
bool dropEnabled();

// From loop(), after the frame is in leds[] and its brightness is set, just
// before FastLED.show(), every frame the lights are on. hrShowing: this frame
// is the heart rate (not a show); a drop only counts then. Starts the ring on
// a drop; with DROP_ANIM 1 (or in test mode) draws the rainbow burst (and the
// meter) over leds[] and sets the brightness for it under capMw. Returns true
// if it drew anything. Also applies mic gain changes (I2C stays on loop()).
bool dropDraw(CRGB *leds, uint8_t wheelBrightness, uint32_t capMw, bool hrShowing);

// How far into the drop rings we are, in ms since the first started, or -1
// when they're over. The face code draws them (DROP_RING_COUNT of them,
// DROP_RING_GAP_MS apart, each DROP_RING_MS long).
int dropRingAgeMs();

// After the 2 s status line: one "drop: ..." line when on, nothing when off.
void dropPrintStatus();
