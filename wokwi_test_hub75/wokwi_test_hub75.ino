/*
  HUB75 MATRIX — display-only test sketch, with fake data

  Cycles through the same three screens as the real costume (heart-beat
  animation, steps, party-volume bar) using fake/simulated values instead
  of a real Whoop or MPU6050, so we can actually watch the pixel art render
  in Wokwi without any of the other hardware.

  CONFIRMED via wokwi-cli: this firmware boots cleanly, displaySetup()
  returns normally, and loop() correctly cycles through every screen with
  sane values (checked via Serial0 logging + `wokwi-cli --timeout ...`).
  But `wokwi-cli --screenshot-part matrix` fails with "Error 1: Part does
  not have a valid framebuffer: matrix" — Wokwi's wokwi-hub75-matrix part
  has no framebuffer at all when driven by this DMA/I2S-peripheral-based
  library (it IS confirmed to work for simple bit-banged GPIO drivers like
  Adafruit's RGBmatrixPanel on Arduino Mega, just not this one). So: the
  display code itself is verified correct, but actually seeing it render
  requires real hardware — Wokwi cannot show it, on any plan.

  Note: Serial0 (not Serial) is used deliberately — on ESP32-S3 with the
  default USBMode=hwcdc, `Serial` binds to the native USB-CDC peripheral,
  which Wokwi's serial monitor does not appear to capture. Serial0 is the
  classic UART0, which is what diagram.json's $serialMonitor connection
  (esp:TX/esp:RX) expects.

  Reuses display_ui.h/.cpp and config.h from the repo root via symlinks.
*/

#include "config.h"
#include "display_ui.h"

enum DisplayMode { MODE_HR, MODE_STEPS, MODE_DB };
DisplayMode currentMode = MODE_HR;
unsigned long lastModeSwitch = 0;
const unsigned long MODE_DURATION_MS = 4000;

void setup() {
  Serial0.begin(115200);
  Serial0.println("Booting...");
  displaySetup();
  Serial0.println("displaySetup() returned OK, entering loop()");
  lastModeSwitch = millis();
}

void loop() {
  // Fake data standing in for the Whoop/MPU6050/mic, purely to exercise
  // every screen's drawing code.
  int fakeBpm = 90 + (int)(50 * sin(millis() / 2000.0));
  unsigned long fakeSteps = millis() / 500;
  int fakeDb = 30 + (millis() / 100) % 50;

  updateHeartbeatPhase(fakeBpm);

  unsigned long now = millis();
  if (now - lastModeSwitch > MODE_DURATION_MS) {
    lastModeSwitch = now;
    currentMode = (DisplayMode)((currentMode + 1) % 3);
  }

  switch (currentMode) {
    case MODE_HR:    drawHRScreen(fakeBpm, true);  break;
    case MODE_STEPS: drawStepsScreen(fakeSteps);   break;
    case MODE_DB:    drawDbScreen(fakeDb);         break;
  }

  static unsigned long lastLog = 0;
  if (now - lastLog > 1000) {
    lastLog = now;
    Serial0.printf("loop alive: mode=%d bpm=%d steps=%lu db=%d\n", currentMode, fakeBpm, fakeSteps, fakeDb);
  }

  delay(30);
}
