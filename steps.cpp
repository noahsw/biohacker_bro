#include "steps.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

unsigned long stepCount = 0;

namespace {

Adafruit_MPU6050 mpu;
float accelBaseline = 9.8;       // roughly 1g at rest
bool stepArmed = true;
unsigned long lastStepTime = 0;
const unsigned long STEP_DEBOUNCE_MS = 250; // prevents double-counting one step
const float STEP_THRESHOLD = 1.8;           // tune this after wearing it once

} // namespace

void stepSetup() {
  Wire.begin(MPU_SDA, MPU_SCL);
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found — check wiring on IO45/IO46");
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }
}

void stepLoop() {
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
