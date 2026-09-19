/*
  HUB75 MATRIX — display-only test sketch, with fake data

  Cycles through the same three screens as the real costume (heart-beat
  animation, steps, party-volume bar) using fake/simulated values instead
  of a real Whoop or MPU6050, so we can actually watch the pixel art render
  in Wokwi without any of the other hardware.

  Whether this renders anything in Wokwi is an open question: Wokwi's
  wokwi-hub75-matrix part is confirmed to work for HUB75 panels driven by
  simple bit-banged GPIO (e.g. Adafruit's RGBmatrixPanel on Arduino Mega),
  but it's unconfirmed whether it also picks up the ESP32-HUB75-MatrixPanel-
  I2S-DMA library's DMA/I2S-peripheral-driven output. This sketch is the
  test for that.

  Reuses display_ui.h/.cpp and config.h from the repo root via symlinks.
*/

#include "config.h"
#include "display_ui.h"

enum DisplayMode { MODE_HR, MODE_STEPS, MODE_DB };
DisplayMode currentMode = MODE_HR;
unsigned long lastModeSwitch = 0;
const unsigned long MODE_DURATION_MS = 4000;

void setup() {
  Serial.begin(115200);
  displaySetup();
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

  delay(30);
}
