/*
  DISPLAY + STEP COUNTER — isolated test sketch

  NOTE: Wokwi's wokwi-hub75-matrix part exists and wires up fine, but it
  has no framebuffer at all for output from this DMA/I2S-driven library —
  confirmed via `wokwi-cli --screenshot-part`, see ../wokwi_test_hub75/ for
  the full writeup. displaySetup() does NOT hang (confirmed via Serial0 +
  wokwi-cli), it just can't be watched rendering in Wokwi. It's kept as a
  standalone compile target for local Arduino IDE / arduino-cli checks.
  For a version of the step-counting logic that DOES visibly simulate in
  Wokwi, see ../wokwi_test_steps/ instead (MPU6050-only, no display).

  Simulates/tests the HUB75 matrix panel and MPU6050 step counting, without
  the BLE stack loaded. Lighter on RAM than the full biohacker_bro sketch.

  Reuses steps.h/.cpp, display_ui.h/.cpp, and config.h from the repo root via
  symlinks — editing those files here edits the real thing, no copy/paste
  drift.

  Just shows the steps screen on a loop (no mode cycling, no HR/dB screens)
  since those depend on the BLE/mic subsystems this sketch excludes.
*/

#include "config.h"
#include "steps.h"
#include "display_ui.h"

void setup() {
  Serial.begin(115200);
  displaySetup();
  stepSetup();
}

void loop() {
  stepLoop();
  drawStepsScreen(stepCount);
  delay(30);
}
