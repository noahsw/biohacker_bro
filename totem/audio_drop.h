#pragma once

#include <Arduino.h>
#include <FastLED.h>

// ============================================================================
// BEAT DROP: THE BOARD'S MICS, AND THE BURST ON A DROP
// ============================================================================
//
// The board's two mics feed its ES7210 audio chip, which streams to the ESP32
// over I2S. A task on the other core listens to the bass and spots drops
// (drop_detect.h); on a drop the ring gets a ~2.6 s burst over whatever it's
// showing (drop_fx.h). DROP_TEST_MODE swaps the heart rate for a sound meter.
//
// Off by default (DROP_ENABLED 0 in totem_config.h). Off means off: the mics
// aren't powered, no I2S, no task, and nothing on the ring changes.
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
// before FastLED.show(). Draws the burst after a drop (or, in test mode, the
// meter) over leds[] and sets the brightness for it under capMw. Returns true
// if it drew anything. Also applies mic gain changes (I2C stays on loop()).
bool dropDraw(CRGB *leds, uint8_t wheelBrightness, uint32_t capMw);

// After the 2 s status line: one "drop: ..." line when on, nothing when off.
void dropPrintStatus();
