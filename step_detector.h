#ifndef STEP_DETECTOR_H
#define STEP_DETECTOR_H

// ============================================================================
// Step detection: acceleration magnitude in, "was that a step?" out
// ============================================================================
//
// Split out of steps.cpp so tests/ can drive it with synthetic signals: the
// sensor calls need the Adafruit library and a live GY-521, this needs
// nothing. steps.cpp does the I2C and passes |a| in; every decision about what
// counts as a step lives here, once.
//
// Header-only for the same reason as steps_layout.h: nothing to add to the
// sketch build beyond a symlink.
//
// Every constant below was set on real hardware during first bring-up (see
// README, "Step counter bring-up"); the tests pin the behaviour each one was
// chosen for.

// Tuned on hardware: stillness peaks <=0.3 m/s^2, hand-held steps 1.0-2.5.
const float STEP_THRESHOLD = 0.9f;

// Below 450, a slow (~1 step/s) walk counted heel strike and push-off as two
// steps: 29 for 20 at 350ms. 450 still allows ~2.2 steps/s.
const unsigned long STEP_DEBOUNCE_MS = 450;

// Slow enough that a step's ~100ms spike barely moves the baseline, fast
// enough to follow a tilt: cheap GY-521s read a different "1g" per
// orientation, and tilting after boot left stillness reading ~1.1 m/s^2
// against a fixed 9.8.
const float BASELINE_TAU_MS = 2000.0f;

struct StepDetector {
  float baseline = 9.8f;           // 1g until seed() measures this sensor's own
  unsigned long lastUpdate = 0;
  unsigned long lastStep = 0;
  bool armed = true;

  // Call once with the average |a| taken while the sensor is still.
  void seed(float mag, unsigned long now) {
    baseline = mag;
    lastUpdate = now;
  }

  // The exact quantity compared against STEP_THRESHOLD.
  float delta(float mag) const {
    float d = mag - baseline;
    return d < 0 ? -d : d;
  }

  // Feed one sample; returns true if it completes a step.
  bool update(float mag, unsigned long now) {
    float d = delta(mag);

    float alpha = (float)(now - lastUpdate) / BASELINE_TAU_MS;
    if (alpha > 1.0f) alpha = 1.0f;
    baseline += (mag - baseline) * alpha;
    lastUpdate = now;

    bool step = false;
    if (d > STEP_THRESHOLD && armed && (now - lastStep) > STEP_DEBOUNCE_MS) {
      lastStep = now;
      armed = false;
      step = true;
    }
    if (d < STEP_THRESHOLD * 0.5f) {
      armed = true; // re-arm once motion settles, so one step = one count
    }
    return step;
  }
};

#endif
