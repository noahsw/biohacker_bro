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

// --- 2b. HUB75 pin mapping — CONFIRMED for this board ---
// The board is sold by WatangTech but its model is Seengreat's "RGB Matrix
// HUB75 S3" (confirmed against the spec sheet: same model name, same
// ESP32-S3-WROOM-1-N16R8, 16MB/8MB, 2x USB-C, VH-4P 5V/4A out, 2x HUB75,
// ES7210 + ES8311, SD + PCF85063 RTC). Mapping is that model's wiki table:
//   https://seengreat.com/wiki/214/rgb-matrix-hub75-s3
// CORRECTED ON HARDWARE: the wiki's table has G and B transposed. It lists
// G1=IO4 / B1=IO6 and G2=IO7 / B2=IO17, but with those values a full-screen
// green renders blue and blue renders green, on both row halves. The values
// below are the wiki's with G and B swapped, verified against the
// white/red/green/blue solid test in wokwi_test_hub75. Everything else in
// that table (R, A-E, CLK, LAT, OE) was correct as published.
#define R1_PIN  5
#define G1_PIN  6
#define B1_PIN  4
#define R2_PIN  15
#define G2_PIN  17
#define B2_PIN  7
#define A_PIN   8
#define B_PIN   18
#define C_PIN   10
#define D_PIN   9
// E is wired to IO16 on this board, but the Waveshare RGB-Matrix-P2.5-64x32
// is 1/16 scan (confirmed in its user guide) and so only uses A-D. Leave at
// -1; set to 16 only if you ever drive a 1/32-scan panel such as a 64x64.
#define E_PIN   -1
#define LAT_PIN 11   // LAT / STB
#define OE_PIN  13
#define CLK_PIN 12

// --- 3. MPU6050 (accelerometer) — confirmed pins from earlier in build ---
#define MPU_SDA 45
#define MPU_SCL 46

// --- 4. Mic I2S pins — from the same vendor wiki (ES7210 ADC side) ---
// The ES7210 (mic ADC) and ES8311 (speaker codec) share one I2S bus:
//   MCLK=IO38, SCLK/BCLK=IO48, LRCK/WS=IO21, DSDIN(to speaker)=IO14,
//   SDOUT(from mics)=IO47
// We only want to LISTEN, so DATA_IN is SDOUT. Still unverified on hardware,
// and the ES7210 likely needs I2C register init before it outputs anything —
// hence MIC_CONFIGURED stays false until it's actually tested.
#define MIC_MCLK_PIN  38
#define MIC_BCLK_PIN  48
#define MIC_WS_PIN    21
#define MIC_DATA_PIN  47
#define MIC_CONFIGURED false  // flip to true once real audio is confirmed

// --- 5. HR zone thresholds (BPM) ---
// Deliberately skewed HIGHER than a standard training-zone chart: this is worn
// while DANCING, not doing intervals, so the usual "zone 4 by 150bpm" scale
// would peg the bar red all night and stop telling you anything. Tuned so a
// casual shuffle sits in Z1-Z2 and genuinely going for it lands in Z3 (green),
// leaving Z4/5 as a rarely-hit ceiling.
//   Z0 <95 | Z1 95-114 | Z2 115-134 | Z3 135-159 | Z4/5 160+
#define ZONE1_BPM 95
#define ZONE2_BPM 115
#define ZONE3_BPM 135
#define ZONE4_BPM 160
// The top of the Z4/5 band, used only to compute progress ACROSS that last
// segment of the bar (there's no zone above it to spill into).
#define ZONE_MAX_BPM 190
