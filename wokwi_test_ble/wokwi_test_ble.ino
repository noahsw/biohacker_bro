/*
  BLE HEART RATE — isolated test sketch

  NOTE: Wokwi does not implement Bluetooth at all in its ESP32 simulation
  (confirmed in Wokwi's own docs, https://docs.wokwi.com/guides/esp32) — so
  this sketch can't actually be simulated there, on any plan. It's kept as
  a standalone compile target for local Arduino IDE / arduino-cli checks,
  and as the real code you'll run on hardware once your Whoop MAC is set.

  Simulates/tests just the Whoop BLE connection and BPM parsing, without the
  HUB75 display or MPU6050 libraries loaded. Lighter on RAM than the full
  biohacker_bro sketch.

  Reuses ble_heart_rate.h/.cpp and config.h from the repo root via symlinks —
  editing those files here edits the real thing, no copy/paste drift.

  Prints connection status + BPM over Serial instead of drawing to a panel.
*/

#include "config.h"
#include "ble_heart_rate.h"

void setup() {
  Serial.begin(115200);
  bleSetup();
}

void loop() {
  bleLoop();

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    Serial.printf("Connected: %s  BPM: %d\n", hrConnected ? "yes" : "no", currentBPM);
  }

  delay(10);
}
