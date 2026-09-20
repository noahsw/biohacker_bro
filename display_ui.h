#pragma once

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// ============================================================================
// DISPLAY: HEART ANIMATION + TEXT
// ============================================================================

extern MatrixPanel_I2S_DMA *display;

// Creates and configures the HUB75 panel (using pins/dims from config.h).
// Returns what the library's begin() reported: false means the DMA/PSRAM
// buffer allocation failed, which is a completely different problem from a
// wrong pin mapping — worth logging during bring-up.
bool displaySetup();

// Call every loop() iteration with the latest BPM to advance the beat animation.
void updateHeartbeatPhase(int bpm);

void drawHRScreen(int bpm, bool connected);
void drawStepsScreen(unsigned long steps);
void drawDbScreen(int dbLevel);
