/*
  STEP COUNTER — LIVE HARDWARE BRING-UP

  Not a Wokwi sketch (see wokwi_test_steps/ for that one). This runs on the
  real board with a real GY-521 attached, and exists to answer the two
  questions bring-up actually asks:

    1. Is the MPU6050 even on the bus?  -> I2C scan, printed every pass.
    2. What is STEP_THRESHOLD competing against? -> prints the peak
       acceleration delta seen since the last line, which is the exact
       quantity steps.cpp compares to STEP_THRESHOLD.

  Diagnostics reprint from loop(), not setup(), on purpose: USB-CDC
  re-enumerates on reset, so anything printed in setup() is usually
  already gone by the time a serial capture attaches (see README).

  Watch it with:
    stty -f /dev/cu.usbmodem201101 115200 raw && cat /dev/cu.usbmodem201101
*/

#include <Wire.h>

#include "config.h"
#include "steps.h"

namespace {

float peakDelta = 0.0f;
unsigned long lastPrint = 0;

void i2cScan() {
  int found = 0;
  Serial.print("I2C scan on SDA=IO");
  Serial.print(MPU_SDA);
  Serial.print(" SCL=IO");
  Serial.print(MPU_SCL);
  Serial.print(" ->");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", addr);
      found++;
    }
  }
  // 0x68 is the MPU6050's default; it moves to 0x69 if AD0 is pulled high.
  Serial.println(found ? "" : " (nothing — check VCC/GND/SDA/SCL)");
}

} // namespace

void setup() {
  Serial.begin(115200);
  stepSetup();
}

void loop() {
  stepLoop();

  float d = stepAccelDelta();
  if (d > peakDelta) peakDelta = d;

  if (millis() - lastPrint > 500) {
    lastPrint = millis();

    if (!stepSensorOk()) {
      // Rescan rather than just repeating the failure: this lets you reseat
      // a jumper and watch the address appear without reflashing.
      i2cScan();
      Serial.println("MPU6050 not initialized — fix wiring, then reset.");
    } else {
      Serial.printf("steps=%lu  peak delta=%.2f m/s^2\r\n", stepCount, peakDelta);
    }
    peakDelta = 0.0f;
  }

  delay(10);
}
