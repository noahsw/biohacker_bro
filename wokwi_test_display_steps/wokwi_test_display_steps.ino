/*
  DISPLAY + STEP COUNTER — isolated test sketch

  Draws the REAL main screen (drawMainScreen) with a simulated heart rate, so
  the actual layout can be seen on the actual panel without a Whoop in range.
  This is the sketch to flash while iterating on the layout.

  It used to call drawStepsScreen() instead, which meant the one sketch whose
  job is previewing the display never drew the layout the costume actually
  uses — and because --gc-sections then dropped drawMainScreen entirely, the
  binary size never moved when the layout changed, which is a quiet way to
  believe you've tested something you haven't.

  Steps walk a ladder of magnitudes (0, 7, 42, 386, 1234, 12345, 99999,
  123456) three seconds apart when no accelerometer is attached, so every
  digit count and the 5-digit cap can be checked without walking anywhere.

  The BPM typeface is FreeMonoBold12pt7b, picked on hardware over FreeSans
  (too curvy), seven-segment (too blocky) and FreeMonoBold18pt (63px for three
  digits, so no room for the heart).

  A solid-panel test used to run for the first 8 seconds here, to rule out a
  panel fault when glyphs looked like they had pixels missing. It did its job
  (the panel is clean; the artefacts were the seven-segment renderer) and has
  been removed. drawSolidTest() is still in display_ui if it's ever needed
  again — call it from setup() and hold.

  The BPM sweep covers the full zone range so the zone colors, the bar fill
  and the digit-width changes at 99->100 can all be checked in one pass. It is
  NOT a heart rate; it's a ramp chosen to exercise the drawing code.

  NOTE: Wokwi's wokwi-hub75-matrix part exists and wires up fine, but it
  has no framebuffer at all for output from this DMA/I2S-driven library —
  confirmed via `wokwi-cli --screenshot-part`, see ../wokwi_test_hub75/ for
  the full writeup. displaySetup() does NOT hang (confirmed via Serial0 +
  wokwi-cli), it just can't be watched rendering in Wokwi. On real hardware
  it renders normally.

  Reuses steps.h/.cpp, display_ui.h/.cpp, and config.h from the repo root via
  symlinks — editing those files here edits the real thing, no copy/paste
  drift.
*/

#include "config.h"
#include "steps.h"
#include "display_ui.h"

namespace {

int sweepBpm();   // defined below

// Alternates a smooth sweep with a "digit parade" (111, 222, ... 999) so every
// digit shape gets shown three times at a readable dwell. The sweep is for
// checking zone colours and the bar; the parade is for checking glyphs.
int simulatedBpm() {
  unsigned long t = millis();
  if ((t / 10000) % 2 == 1) {                  // parade for 10s in every 20
    int k = 1 + (int)((t % 10000) / 1100);     // 1..9
    if (k > 9) k = 9;
    return k * 111;
  }
  return sweepBpm();
}

// Sweeps REST_BPM..ZONE_MAX_BPM and back, about once every 20 seconds.
int sweepBpm() {
  const unsigned long PERIOD_MS = 20000;
  float phase = (millis() % PERIOD_MS) / (float)PERIOD_MS;      // 0..1
  float tri = phase < 0.5f ? (phase * 2.0f) : (2.0f - phase * 2.0f);
  return REST_BPM + (int)(tri * (ZONE_MAX_BPM - REST_BPM));
}

// Steps aren't simulated as a smooth ramp: the interesting thing to look at
// is how each DIGIT COUNT renders and right-aligns, so this walks a ladder of
// magnitudes, three seconds each, including the 99999 cap.
unsigned long simulatedSteps() {
  static const unsigned long LADDER[] = {0, 7, 42, 386, 1234, 12345, 99999, 123456};
  const int N = sizeof(LADDER) / sizeof(LADDER[0]);
  return LADDER[(millis() / 3000) % N];
}

// The bake-off is over: the built-in 5x7 at size 2, the same face as the step
// count. FreeSans read as too curvy, seven-segment as too blocky, and
// FreeMonoBold18pt7b is 63px for three digits — the whole panel, no room for
// the heart. See display_ui.cpp for the full record.
//
// Left as a one-element array rather than deleted: BpmStyle still exists, and
// widening this is how you'd run another comparison.
BpmStyle simulatedStyle() {
  static const BpmStyle CANDIDATES[] = {BPM_BUILTIN_2};
  const int N = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);
  return CANDIDATES[(millis() / 10000) % N];
}

} // namespace

void setup() {
  Serial.begin(115200);
  displaySetup();
  stepSetup();
}

void loop() {
  stepLoop();

  int bpm = simulatedBpm();
  updateHeartbeatPhase(bpm);           // drives the heart's pulse envelope

  // Real steps if a sensor is attached, simulated ladder if not.
  unsigned long steps = stepSensorOk() ? stepCount : simulatedSteps();
  BpmStyle style = simulatedStyle();
  setBpmStyle(style);
  drawMainScreen(bpm, true, steps);

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    // Reprinted from loop() because USB-CDC re-enumerates on reset and
    // anything printed in setup() is usually gone before a capture attaches.
    Serial.printf("font=%-17s bpm=%3d  steps=%-6lu mpu=%s  beat=%.2f\r\n",
                  bpmStyleName(style), bpm, steps,
                  stepSensorOk() ? "ok" : "ABSENT", heartBeatLevel());
  }

  delay(15);
}
