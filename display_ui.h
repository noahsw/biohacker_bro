#pragma once

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// ============================================================================
// DISPLAY: THE MAIN SCREEN
// ============================================================================

extern MatrixPanel_I2S_DMA *display;

// Creates and configures the HUB75 panel (using pins/dims from config.h).
// Returns what the library's begin() reported: false means the DMA/PSRAM
// buffer allocation failed, which is a completely different problem from a
// wrong pin mapping — worth logging during bring-up.
bool displaySetup();

// Call every loop() iteration with the latest BPM to advance the beat animation.
void updateHeartbeatPhase(int bpm);

// The one and only screen: beating heart, BPM, steps, and the HR-zone bar.
void drawMainScreen(int bpm, bool connected, unsigned long steps);

// How the BPM number is drawn. This exists so the candidates can be compared
// on the real panel in one flash instead of one reflash each — stroke weight
// and legibility at 2.5mm pitch are not things a simulator can answer.
// Once you've picked, collapse this to the winner and delete the rest.
enum BpmStyle {
  BPM_BUILTIN_2 = 0, // built-in 5x7 at size 2 — 14px tall, 34px, uniform.
                     // The same blocky face as the step count. Picked on
                     // hardware over every real typeface below.
  BPM_SANS,          // FreeSans12pt7b      — 17px tall, 39px, uniform
  BPM_SANS_BOLD,     // FreeSansBold12pt7b  — 17px tall, up to 42px, NOT uniform
  BPM_MONO_BOLD,     // FreeMonoBold12pt7b  — 15px tall, 42px, uniform, squarer
  BPM_SEVEN_SEG,     // drawn from rectangles — 16px tall, 31px, symmetric bowls
  BPM_STYLE_COUNT
};
void setBpmStyle(BpmStyle style);
const char *bpmStyleName(BpmStyle style);

// Fills the panel with one colour and returns. A strip of dead pixels through
// a glyph is usually not the glyph: rows 15/16 are the boundary between this
// panel's two scan halves, and the BPM number is the only element that
// crosses it. Fill the panel solid and the seam either shows up or doesn't,
// which separates a font problem from a panel problem in one look.
void drawSolidTest(uint8_t r, uint8_t g, uint8_t b);

// Current value of the heartbeat brightness envelope, 0.0 (relaxed) to 1.0
// (full thump). Exposed for diagnostics: if the heart doesn't look like it's
// beating, print this to find out whether the envelope is flat (a timing bug)
// or moving (a brightness/perception problem) before changing anything.
float heartBeatLevel();

// The color the BPM number is drawn in for this heart rate. The zone math
// itself lives in hr_zones.h, which is hardware-free and unit-tested.
uint16_t zoneColor(int bpm);

// Kept for the isolated test sketches, which have no BLE subsystem.
void drawStepsScreen(unsigned long steps);
