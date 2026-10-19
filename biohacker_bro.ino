/*
  ============================================================================
  BIOHACKER BRO — Halloween Costume Display
  ============================================================================
  Hardware:
    - WatangTech ESP32-S3 HUB75 RGB Matrix Controller
    - Waveshare 64x32 RGB LED Matrix Panel (HUB75, 2.5mm pitch)
    - Whoop strap (HR Broadcast enabled in Whoop app) — read via BLE
    - GY-521 (MPU-6050) accelerometer — on the I2C expansion connector,
      IO1 (SDA) / IO2 (SCL). NOT IO45/IO46; see config.h for why.

  What this does:
    - Connects ONLY to your specific Whoop (filtered by MAC address, so it
      ignores anyone else's Whoop broadcasting nearby at the party)
    - Shows ONE fixed screen (no cycling): live BPM in your current HR
      zone's color, a heart icon beating in time with your actual heart
      rate, a step count, and a zone bar along the top two rows under a
      permanently-lit 5-segment zone legend
    - Counts steps in real time using the accelerometer

  Code is split across a few files so each subsystem can be understood (and
  tested) on its own:
    - config.h          — all pins/constants you may need to edit
    - ble_heart_rate.*   — Whoop BLE heart-rate client
    - steps.*            — MPU6050 step counting
    - display_ui.*       — HUB75 panel setup + the main screen layout
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
#include "display_ui.h"
#include "brightness.h"

// ============================================================================
// SETUP / LOOP
// ============================================================================
//
// One screen, always showing the same thing — no rotation. The panel is worn
// on a costume, where anyone glancing at it gets about one second of
// attention: a display that cycles guarantees that second lands on the wrong
// screen. So HR, steps and zone all live on the one layout at once. See
// display_ui.cpp for the pixel budget that makes them fit.

void setup() {
  Serial.begin(115200);
  // Drop serial output rather than wait for it. Plugged into a computer with
  // no monitor open, the USB port is "connected" but nobody is reading, and
  // by default each print then blocks for up to ~2s (20 retries x 100ms)
  // waiting for room. That froze the heart once per status line, every 2s.
  // On battery there's no host and the core never waits, but a freeze that
  // only happens on the bench still makes the bench lie.
  Serial.setTxTimeoutMs(0);

  displaySetup();
  stepSetup();
  brightnessSetup();  // after stepSetup(): the wheel is on its I2C bus
  bleSetup();
}

void loop() {
  bleLoop();
  stepLoop();
  updateHeartbeatPhase(currentBPM);

  drawMainScreen(currentBPM, hrConnected, stepCount);

  // Periodic status line. The BLE client only logs on failure or disconnect,
  // so without this a silent serial port means either "scanning happily" or
  // "connected and working" and there is no way to tell them apart.
  //
  // Printed from loop() rather than setup() because USB-CDC re-enumerates on
  // reset, so a monitor attaching afterwards has already missed setup().
  // Watch it with tools/serial_monitor.py (plain `cat` won't raise DTR).
  // beatMin/beatMax bracket the heartbeat envelope over the whole reporting
  // window, not a single sample: one instantaneous reading can't distinguish
  // "flat" from "caught between beats", which is the exact question.
  static unsigned long lastStatus = 0;
  static float beatMin = 1.0f, beatMax = 0.0f;
  static unsigned long frames = 0;
  // Longest gap between two frames, this window and since boot. fps alone
  // averages a stall away: one 300ms freeze every 2s still reads as ~45fps.
  static unsigned long lastFrame = 0, maxGap = 0, worstGap = 0;
  unsigned long nowFrame = millis();
  if (lastFrame) {
    unsigned long gap = nowFrame - lastFrame;
    if (gap > maxGap) maxGap = gap;
    if (gap > worstGap) worstGap = gap;
  }
  lastFrame = nowFrame;
  float lvl = heartBeatLevel();
  if (lvl < beatMin) beatMin = lvl;
  if (lvl > beatMax) beatMax = lvl;
  frames++;

  if (millis() - lastStatus > 2000) {
    unsigned long dt = millis() - lastStatus;
    Serial.printf("hr=%-9s bpm=%3d  steps=%lu  mpu=%s  beat=%.2f..%.2f  fps=%lu gap=%lu/%lums  wheel=0x%02X raw=0x%02X err=%lu bright=%d\r\n",
                  hrConnected ? "CONNECTED" : "scanning", currentBPM, stepCount,
                  stepSensorOk() ? "ok" : "ABSENT", beatMin, beatMax,
                  dt ? frames * 1000UL / dt : 0, maxGap, worstGap,
                  brightnessWheelAddr(), brightnessWheelRaw(),
                  brightnessWheelErrors(), brightnessLevel());
    lastStatus = millis();
    beatMin = 1.0f; beatMax = 0.0f; frames = 0; maxGap = 0;
  }

  // ~60fps, so the heart's brightness envelope stays smooth. Not a plain
  // delay(): the wheel is read throughout the wait, see brightness.h.
  brightnessWait(15);
}
