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

// Which zone (0..4) a BPM falls in, and that zone's color. Exposed mostly so
// other code (and the test sketches) can stay consistent with the bar.
int hrZone(int bpm);
uint16_t zoneColor(int bpm);

// Kept for the isolated test sketches, which have no BLE/mic subsystem.
void drawStepsScreen(unsigned long steps);
void drawDbScreen(int dbLevel);
