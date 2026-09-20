/*
  ============================================================================
  BIOHACKER BRO — Halloween Costume Display
  ============================================================================
  Hardware:
    - WatangTech ESP32-S3 HUB75 RGB Matrix Controller
    - Waveshare 64x32 RGB LED Matrix Panel (HUB75, 2.5mm pitch)
    - Whoop strap (HR Broadcast enabled in Whoop app) — read via BLE
    - GY-521 (MPU-6050) accelerometer — wired to IO45 (SDA) / IO46 (SCL)
    - Onboard mic (ES7210 codec) — for relative decibel level

  What this does:
    - Connects ONLY to your specific Whoop (filtered by MAC address, so it
      ignores anyone else's Whoop broadcasting nearby at the party)
    - Displays live BPM with a heart icon that beats in time with your
      actual heart rate, colored by HR zone (green/yellow/red)
    - Counts steps in real time using the accelerometer
    - Shows a relative "party volume" bar from the onboard mic
    - Cycles through HR / Steps / Decibels on the display

  Code is split across a few files so each subsystem can be understood (and
  tested) on its own:
    - config.h          — all pins/constants you may need to edit
    - ble_heart_rate.*   — Whoop BLE heart-rate client
    - steps.*            — MPU6050 step counting
    - mic.*              — decibel/mic level (placeholder until I2S pins known)
    - display_ui.*       — HUB75 panel setup + the three screens
  See wokwi_test_ble/ and wokwi_test_display_steps/ for smaller sketches that
  simulate just one subsystem at a time (they reuse these same files via
  symlinks — no duplicated code).

  ============================================================================
  BEFORE YOU FLASH THIS — things only YOU can fill in (see config.h):
  ============================================================================
  1. TARGET_WHOOP_MAC — run a BLE scan once at home (quiet room, only your
     Whoop broadcasting) and paste your Whoop's MAC address in. Without this,
     it will connect to ANY Whoop it sees first — including a friend's at
     the party.

  2. HUB75 pin mapping — the ESP32-HUB75-MatrixPanel-I2S-DMA library needs
     to know which GPIO pins on YOUR board connect to which HUB75 signal
     (R1, G1, B1, R2, G2, B2, A, B, C, D, E, CLK, LAT, OE). I do not have
     WatangTech's exact schematic, so the pin numbers in config.h are a
     REASONABLE GUESS based on common ESP32-S3 HUB75 board layouts —
     NOT confirmed for your specific board. Check the product's wiki/
     GitHub page (WatangTech usually links one), or send me a photo of
     the board's silkscreen labels near the HUB75 header once it arrives,
     and I'll correct these.

  3. Mic I2S pins — the ES7210 audio codec on your board talks over I2S,
     but I do not have confirmed GPIO numbers for BCLK/WS/DATA on this
     board. This is genuinely vendor-specific and I don't want to guess
     wrong on something you'd have to debug blind. I've written the
     decibel code so it's ready to go the moment you have those 3 pin
     numbers — either from the board's documentation or by asking the
     seller. Until then, the dB reading will show a placeholder pattern
     instead of real audio, so the rest of the build isn't blocked on it.

  ============================================================================
*/

#include "config.h"
#include "ble_heart_rate.h"
#include "steps.h"
#include "mic.h"
#include "display_ui.h"

// ============================================================================
// DISPLAY CYCLING
// ============================================================================

enum DisplayMode { MODE_HR, MODE_STEPS, MODE_DB };
DisplayMode currentMode = MODE_HR;
unsigned long lastModeSwitch = 0;
const unsigned long MODE_DURATION_MS = 4000; // 4 seconds per screen

// ============================================================================
// SETUP / LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);

  displaySetup();
  stepSetup();
  micSetup();
  bleSetup();

  lastModeSwitch = millis();
}

void loop() {
  bleLoop();
  stepLoop();
  int dbLevel = micLoop();
  updateHeartbeatPhase(currentBPM);

  // Cycle screens every few seconds
  unsigned long now = millis();
  if (now - lastModeSwitch > MODE_DURATION_MS) {
    lastModeSwitch = now;
    currentMode = (DisplayMode)((currentMode + 1) % 3);
  }

  switch (currentMode) {
    case MODE_HR:    drawHRScreen(currentBPM, hrConnected); break;
    case MODE_STEPS: drawStepsScreen(stepCount);            break;
    case MODE_DB:    drawDbScreen(dbLevel);                 break;
  }

  delay(30); // ~30fps-ish refresh of our drawing logic
}
