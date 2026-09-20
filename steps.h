#pragma once

#include <Arduino.h>

// ============================================================================
// STEP COUNTING (MPU6050)
// ============================================================================

extern unsigned long stepCount;

void stepSetup();
void stepLoop();

// True only if the MPU6050 answered on I2C at stepSetup(). When false,
// stepLoop() does nothing and stepCount stays frozen at 0.
bool stepSensorOk();

// Current |acceleration| minus the 1g baseline — the exact quantity
// STEP_THRESHOLD is compared against. Exposed for threshold tuning on
// real hardware; does its own sensor read, so don't call it in the hot
// path alongside stepLoop() unless you're tuning.
float stepAccelDelta();
