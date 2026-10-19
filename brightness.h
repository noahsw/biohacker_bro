#pragma once

#include <Arduino.h>

// ============================================================================
// BRIGHTNESS: SET WITH THE BOARD'S THUMB WHEEL
// ============================================================================
//
// The 3-way thumb wheel at the top edge of the board, silkscreened K1/K2/K3:
// roll it one way for brighter, the other for dimmer, or press it in to step
// through BRIGHTNESS_LEVELS (config.h) and wrap round. The choice is saved to
// NVS flash, so a reboot or battery swap comes back at the same level.
//
// The wheel is NOT on an ESP32 pin. It's wired to the onboard PCA9557 I2C
// I/O expander, which shares the IO1/IO2 bus with the MPU6050 and the RTC —
// so this has to run after stepSetup() has started Wire.

// Call after displaySetup() and stepSetup(): finds the PCA9557, restores the
// saved level and applies it.
void brightnessSetup();

// Use in place of loop()'s delay(): waits `ms`, reading the wheel every few
// ms throughout, then saves any pending change. The wait is where the wheel
// gets read because a flick is shorter than a frame — polled once per loop()
// iteration, most flicks were missed outright.
void brightnessWait(unsigned long ms);

// For the periodic status line: the PCA9557's I2C address (0 = not found)
// and the current level, 1-based. setup() output is usually missed — USB-CDC
// re-enumerates on reset — so these are reprinted from loop().
uint8_t brightnessWheelAddr();
int brightnessLevel();
// Last raw byte read from the expander's input register, and how many reads
// have failed since boot. Idle is 0x0F on the real board.
uint8_t brightnessWheelRaw();
unsigned long brightnessWheelErrors();
