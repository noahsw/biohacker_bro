#pragma once

#include <Arduino.h>

// ============================================================================
// STEP COUNTING (MPU6050)
// ============================================================================

extern unsigned long stepCount;

void stepSetup();
void stepLoop();
