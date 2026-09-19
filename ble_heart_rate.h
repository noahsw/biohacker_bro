#pragma once

#include <Arduino.h>

// ============================================================================
// BLE: HEART RATE CLIENT (filtered to your Whoop's MAC only, see config.h)
// ============================================================================

extern volatile int currentBPM;
extern volatile bool hrConnected;

// Starts scanning for the Whoop and connects when found.
void bleSetup();

// Call every loop() iteration: handles a pending connection attempt.
void bleLoop();
