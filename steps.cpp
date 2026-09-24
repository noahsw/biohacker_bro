#include "steps.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

unsigned long stepCount = STEP_COUNT_START;

namespace {

Adafruit_MPU6050 mpu;
bool mpuOk = false;                 // false => stepLoop() is a no-op
float accelBaseline = 9.8;       // 1g; seeded in stepSetup(), then tracked in stepLoop()
unsigned long lastBaselineUpdate = 0;
// Slow enough that a step's ~100ms spike barely moves it, fast enough to
// follow a tilt: cheap GY-521s read a different "1g" per orientation, and on
// real hardware tilting after boot left stillness reading ~1.1 m/s^2.
const float BASELINE_TAU_MS = 2000.0f;
bool stepArmed = true;
unsigned long lastStepTime = 0;
const unsigned long STEP_DEBOUNCE_MS = 450; // <450 double-counted slow steps (land + push-off) on hardware
const float STEP_THRESHOLD = 0.9;           // tuned on hardware: still <=0.3, steps 1.0-2.5

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
      sensors_event_t a, g, temp;
      mpu.getEvent(&a, &g, &temp);
      sum += sqrt(a.acceleration.x * a.acceleration.x +
                  a.acceleration.y * a.acceleration.y +
                  a.acceleration.z * a.acceleration.z);
      delay(10);
    }
    accelBaseline = sum / samples;
    lastBaselineUpdate = millis();
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
  float dt = (float)(now - lastBaselineUpdate);
  lastBaselineUpdate = now;
  float alpha = dt / BASELINE_TAU_MS;
  if (alpha > 1.0f) alpha = 1.0f;
  accelBaseline += (mag - accelBaseline) * alpha;

  if (delta > STEP_THRESHOLD && stepArmed && (now - lastStepTime) > STEP_DEBOUNCE_MS) {
    stepCount++;
    lastStepTime = now;
    stepArmed = false;
  }
  if (delta < STEP_THRESHOLD * 0.5) {
    stepArmed = true; // re-arm once motion settles, so one step = one count
  }
}
