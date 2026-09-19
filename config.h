#pragma once

// ============================================================================
// CONFIG — EDIT THESE
// ============================================================================

// --- 1. YOUR Whoop's MAC address ---
// Defined in secrets.h (gitignored, not committed) so it never ends up in
// the public repo. Copy secrets.h.example to secrets.h and fill in the real
// value after scanning at home — see find_whoop_mac/.
#include "secrets.h"

// --- 2. HUB75 panel dimensions ---
#define PANEL_WIDTH  64
#define PANEL_HEIGHT 32
#define PANEL_CHAIN  1

// --- 2b. HUB75 pin mapping — UNCONFIRMED for your exact board, see note in biohacker_bro.ino ---
#define R1_PIN  4
#define G1_PIN  5
#define B1_PIN  6
#define R2_PIN  7
#define G2_PIN  15
#define B2_PIN  16
#define A_PIN   17
#define B_PIN   18
#define C_PIN   8
#define D_PIN   3
#define E_PIN   -1   // set to a real pin if your panel needs an E line (1/32 scan panels do)
#define LAT_PIN 40
#define OE_PIN  39
#define CLK_PIN 41

// --- 3. MPU6050 (accelerometer) — confirmed pins from earlier in build ---
#define MPU_SDA 45
#define MPU_SCL 46

// --- 4. Mic I2S pins — TODO: fill in once confirmed for your board ---
#define MIC_BCLK_PIN  -1  // TODO
#define MIC_WS_PIN    -1  // TODO
#define MIC_DATA_PIN  -1  // TODO
#define MIC_CONFIGURED false  // flip to true once the 3 pins above are filled in
