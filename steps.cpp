#include "steps.h"
#include "config.h"
#include "step_detector.h"

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

unsigned long stepCount = STEP_COUNT_START;

namespace {

Adafruit_MPU6050 mpu;
bool mpuOk = false;                 // false => stepLoop() is a no-op
StepDetector detector;

float readMagnitude() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  return sqrt(a.acceleration.x * a.acceleration.x +
              a.acceleration.y * a.acceleration.y +
              a.acceleration.z * a.acceleration.z);
}

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

    // Measure this sensor's own 1g instead of trusting 9.8: cheap GY-521s
    // read ~1 m/s^2 off at rest, which on real hardware ate half of
    // STEP_THRESHOLD and sat right on the re-arm level. Assumes the sensor
    // is still for the ~0.5s after power-on.
    float sum = 0.0f;
    const int samples = 50;
    for (int i = 0; i < samples; i++) {
      sum += readMagnitude();
      delay(10);
    }
    detector.seed(sum / samples, millis());
  }
}

bool stepSensorOk() { return mpuOk; }

float stepAccelDelta() {
  if (!mpuOk) return 0.0f;
  return detector.delta(readMagnitude());
}

void stepLoop() {
  // Without a sensor, getEvent() leaves the event struct untouched and we
  // would count "steps" out of uninitialized stack. Do nothing instead.
  if (!mpuOk) return;
  if (detector.update(readMagnitude(), millis())) stepCount++;
}
