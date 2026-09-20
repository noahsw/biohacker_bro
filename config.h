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

// --- 3. MPU6050 (accelerometer) — on the I2C EXPANSION CONNECTOR, not IO45/46 ---
// Moved from IO45/IO46 to IO1/IO2 for one reason: no soldering. The board's
// IO45/IO46 breakout is bare plated through-holes — no pin header, no socket,
// nothing a jumper wire can grip — so using it means soldering both the board
// and the GY-521 (which also ships with its header loose). The white 4-pin
// 1mm-pitch connector on the left edge, next to the USB-C ports, is a real
// plug-in connector carrying the same I2C bus on IO1/IO2. Vendor wiki:
//   https://seengreat.com/wiki/214/rgb-matrix-hub75-s3
//
// TRAP — the connector's pin order is NOT the Qwiic/STEMMA QT order. The
// silkscreen reads, top to bottom:
//   3V3, GND, IO1 (SDA), IO2 (SCL)
// Qwiic/STEMMA QT is GND, V+, SDA, SCL — power and ground transposed. The
// housings are the same 1mm JST-SH, so a standard Qwiic-to-Qwiic cable mates
// perfectly and feeds 3.3V into the sensor's GND. Seengreat never claimed
// Qwiic compatibility (their wiki just calls it an "I2C expansion connector"),
// so this is two conventions sharing a plug, not a vendor error — but it
// destroys a sensor just the same.
//
// Hence the wiring uses a JST-SH-to-loose-female-sockets cable, so each wire
// is placed on the GY-521 by FUNCTION rather than by connector position. And
// before the sensor is ever attached: plug the cable into a powered board and
// meter which wire is +3.3V and which is 0V. SDA/SCL swapped just means the
// sensor doesn't enumerate; VCC/GND swapped means a dead sensor.
//
// Unlike IO45/IO46, this bus is SHARED with the onboard peripherals (PCF85063
// RTC, ES7210, ES8311, PCA9557). The MPU6050 answers at 0x68 with ADO left
// floating, which none of those use. steps_bringup/ scans the bus and prints
// every address found, so confirm rather than assume.
#define MPU_SDA 1
#define MPU_SCL 2

// --- 4. (was the mic) ---
// The decibel meter was cut: it wants to be a bar, the bottom rows are
// already a bar, and two bars on a 32px-tall panel compete for the same read.
// The I2S pin mapping that was here (MCLK=38, BCLK=48, WS=21, mic SDOUT=47,
// speaker DSDIN=14) was never verified on hardware and the ES7210 still needs
// I2C register init before it would stream anything. It's preserved in git
// history and in the README's "What got cut" section rather than sitting here
// as dead defines.

// --- 5. Step count head start ---
// The wearer does not arrive at the party having taken zero steps, and a
// chest display reading "0 STEPS" at 9pm undercuts the joke. Steps counted
// during the night are ADDED to this.
//
// This is a prop offset, not a measurement, and it is the honest place to say
// so: the number on the panel is this constant plus a jolt count from a
// threshold detector that cannot tell dancing from walking. Set it to 0 if
// you ever want the raw count.
#define STEP_COUNT_START 5000

// --- 6. HR zone thresholds (BPM) ---
// Calibrated to the actual wearer, not to a generic training chart: resting
// HR is 55 and hard dancing peaks around 110. A stock chart would put 110 in
// zone 1 and the bar would sit dead-left all night.
//
// REST_BPM is the bar's zero point, not just zone 0's label. Anchoring the
// scale at 0bpm would mean sitting still already showed the bar 80% of the
// way through zone 0 — the bar should be empty when you're doing nothing.
//
//   Z0 55-69 | Z1 70-84 | Z2 85-99 | Z3 100-114 | Z4/5 115+
//
// Even 15bpm bands, placing a 110bpm peak about two-thirds through zone 3:
// visibly, comfortably in Z3 with headroom left, so a big night can still
// tip into red without Z4 being unreachable.
#define REST_BPM  55
#define ZONE1_BPM 70
#define ZONE2_BPM 85
#define ZONE3_BPM 100
#define ZONE4_BPM 115
// The top of the Z4/5 band, used only to compute progress ACROSS that last
// segment of the bar (there's no zone above it to spill into).
#define ZONE_MAX_BPM 140
