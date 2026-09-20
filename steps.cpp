#include "steps.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

unsigned long stepCount = 0;

namespace {

Adafruit_MPU6050 mpu;
bool mpuOk = false;                 // false => stepLoop() is a no-op
float accelBaseline = 9.8;       // roughly 1g at rest
bool stepArmed = true;
unsigned long lastStepTime = 0;
const unsigned long STEP_DEBOUNCE_MS = 250; // prevents double-counting one step
const float STEP_THRESHOLD = 1.8;           // tune this after wearing it once

} // namespace

void stepSetup() {
  Wire.begin(MPU_SDA, MPU_SCL);
  mpuOk = mpu.begin();
  if (!mpuOk) {
    // Print the pins rather than hardcoding them: this sensor has already
    // moved buses once (IO45/46 -> IO1/2) and a stale pin in an error message
    // sends you to check the wrong connector.
    Serial.printf("MPU6050 not found — check wiring on SDA=IO%d SCL=IO%d\r\n",
                  MPU_SDA, MPU_SCL);
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }
}

bool stepSensorOk() { return mpuOk; }

float stepAccelDelta() {
  if (!mpuOk) return 0.0f;
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float mag = sqrt(a.acceleration.x * a.acceleration.x +
                   a.acceleration.y * a.acceleration.y +
                   a.acceleration.z * a.acceleration.z);
  return fabs(mag - accelBaseline);
}

void stepLoop() {
  // Without a sensor, getEvent() leaves the event struct untouched and we
  // would count "steps" out of uninitialized stack. Do nothing instead.
  if (!mpuOk) return;

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Magnitude of acceleration vector, minus gravity baseline
  float mag = sqrt(a.acceleration.x * a.acceleration.x +
                    a.acceleration.y * a.acceleration.y +
                    a.acceleration.z * a.acceleration.z);
  float delta = fabs(mag - accelBaseline);

  unsigned long now = millis();
  if (delta > STEP_THRESHOLD && stepArmed && (now - lastStepTime) > STEP_DEBOUNCE_MS) {
    stepCount++;
    lastStepTime = now;
    stepArmed = false;
  }
  if (delta < STEP_THRESHOLD * 0.5) {
    stepArmed = true; // re-arm once motion settles, so one step = one count
  }
}
