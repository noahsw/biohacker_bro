/*
  STEP COUNTER — MPU6050-only, Wokwi-simulatable test sketch

  BLE and the HUB75 matrix panel can't be simulated in Wokwi at all:
    - Bluetooth is not implemented in Wokwi's ESP32 simulator (confirmed in
      Wokwi's own docs, https://docs.wokwi.com/guides/esp32), so the Whoop
      BLE connection can never be tested here, on any plan.
    - There is no HUB75 RGB matrix panel part in Wokwi's parts library, so
      there's nothing to wire the display up to or see output on.

  This sketch exists to validate just the step-counting math against
  Wokwi's virtual MPU6050 (wokwi-mpu6050), which IS supported. Open
  diagram.json in the Wokwi VS Code extension, start the simulator, and
  drag the accelerometer sliders to simulate motion/steps.

  Reuses steps.h/.cpp and config.h from the repo root via symlinks — no
  duplicated code.
*/

#include "config.h"
#include "steps.h"

void setup() {
  Serial.begin(115200);
  stepSetup();
}

void loop() {
  stepLoop();

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 300) {
    lastPrint = millis();
    Serial.printf("Steps: %lu\r\n", stepCount);
  }

  delay(10);
}
