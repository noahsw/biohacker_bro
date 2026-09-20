#pragma once

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// ============================================================================
// DISPLAY: HEART ANIMATION + TEXT
// ============================================================================

extern MatrixPanel_I2S_DMA *display;

// Creates and configures the HUB75 panel (using pins/dims from config.h).
void displaySetup();

// Call every loop() iteration with the latest BPM to advance the beat animation.
void updateHeartbeatPhase(int bpm);

void drawHRScreen(int bpm, bool connected);
void drawStepsScreen(unsigned long steps);
void drawDbScreen(int dbLevel);
