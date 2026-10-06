// ============================================================================
// Tests for step detection (step_detector.h)
// ============================================================================
//
// Run with `make -C tests`. Same house style as the other tests: no
// framework, property checks, one binary per subject.
//
// Every case here is a failure that was actually seen during first bring-up
// on the real GY-521, reproduced as a synthetic signal. They are synthetic
// because the bring-up logs only kept the peak per half-second, not the raw
// stream — so these pin the SHAPE of each failure (an offset, a tilt, a
// double bump), not a replay of a real walk.
//
// Signals are |a| in m/s^2, sampled every TICK_MS like the main loop.

#include "../step_detector.h"

#include <cstdarg>
#include <cstdio>

static int failures = 0;
static int checks = 0;

static void checkf(bool ok, const char *fmt, ...) {
  checks++;
  if (!ok) {
    failures++;
    printf("  FAIL: ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
  }
}

// The main sketch's loop period (delay(15) plus work); bring-up runs at 10.
static const unsigned long TICKS_MS[] = {10, 15};

// This board's measured 1g at rest was ~0.9 m/s^2 off 9.8.
static const float G_OFFSET = 10.7f;

// Deterministic noise in [-amp, +amp], so a failure reproduces exactly.
struct Noise {
  unsigned long state = 12345;
  float next(float amp) {
    state = state * 1103515245UL + 12345UL;
    float u = (float)((state >> 16) & 0x7fff) / 32767.0f; // 0..1
    return (u * 2.0f - 1.0f) * amp;
  }
};

// One bump: a 100ms triangle peaking at `amp` above the baseline.
static float bump(unsigned long t, unsigned long start, float amp) {
  if (t < start || t >= start + 100) return 0.0f;
  unsigned long x = t - start;
  return amp * (x < 50 ? x / 50.0f : (100 - x) / 50.0f);
}

// Seeded detector with the clock well past boot, as after stepSetup().
static StepDetector seeded(float g, unsigned long now) {
  StepDetector d;
  d.seed(g, now);
  return d;
}

static void test_still_with_offset_counts_nothing() {
  printf("still, 1g reads %.1f instead of 9.8, noise +-0.3: 0 steps\n", G_OFFSET);
  for (unsigned long tick : TICKS_MS) {
    StepDetector d = seeded(G_OFFSET, 1000);
    Noise n;
    int steps = 0;
    for (unsigned long t = 1000; t < 61000; t += tick)
      steps += d.update(G_OFFSET + n.next(0.3f), t);
    checkf(steps == 0, "tick %lums: %d steps in 60s of stillness", tick, steps);
  }
}

static void test_tilt_after_boot_settles() {
  // Seeded at one orientation, then held at another reading 1.1 higher —
  // what left stillness at ~1.1 m/s^2 on hardware with a fixed baseline.
  printf("tilted after boot: at most 1 count for the tilt, then 0 while still\n");
  for (unsigned long tick : TICKS_MS) {
    StepDetector d = seeded(9.8f, 1000);
    int atTilt = 0, after = 0;
    for (unsigned long t = 1000; t < 41000; t += tick) {
      bool s = d.update(10.9f, t);
      if (t < 11000) atTilt += s; else after += s;
    }
    checkf(atTilt <= 1, "tick %lums: tilt counted %d steps", tick, atTilt);
    checkf(after == 0, "tick %lums: %d steps after settling", tick, after);
    checkf(d.delta(10.9f) < 0.1f, "tick %lums: settled delta %.2f, want ~0",
           tick, d.delta(10.9f));
  }
}

static void test_walk_counts_every_step() {
  // Hand-held steps peaked 1.0-2.5; 1.0-1.7 was what the old 1.8 missed.
  // Cadence from a slow 1/s to a brisk 2/s.
  //
  // Starts at 1.2, not 1.0, on purpose. A 100ms bump peaking at 1.0 is above
  // 0.9 for only ~10ms, so at a 15ms tick it's mostly sampled on either side
  // of the peak and missed (7-11 of 20 when tried). That's a real limit:
  // steps that barely clear the threshold are a coin flip. The logged "1.0"
  // peaks were themselves samples, so the true peaks were likely higher.
  printf("20 steps, peaks 1.2-2.5, 1-2 steps/s: exactly 20\n");
  const float amps[] = {1.2f, 1.5f, 2.5f};
  const unsigned long periods[] = {1000, 700, 500};
  for (unsigned long tick : TICKS_MS)
    for (float amp : amps)
      for (unsigned long period : periods) {
        StepDetector d = seeded(G_OFFSET, 1000);
        Noise n;
        int steps = 0;
        unsigned long start = 2000, end = start + 20 * period + 2000;
        for (unsigned long t = 1000; t < end; t += tick) {
          float a = 0.0f;
          for (int i = 0; i < 20; i++) a += bump(t, start + i * period, amp);
          steps += d.update(G_OFFSET + a + n.next(0.1f), t);
        }
        checkf(steps == 20, "tick %lums amp %.1f period %lums: %d steps", tick,
               amp, period, steps);
      }
}

static void test_heel_and_push_off_count_once() {
  // A slow step is two bumps ~300ms apart; at 350ms debounce that counted
  // 29 for 20 on hardware.
  printf("slow steps as two bumps 300ms apart: 20 steps, not 40\n");
  for (unsigned long tick : TICKS_MS) {
    StepDetector d = seeded(G_OFFSET, 1000);
    int steps = 0;
    unsigned long start = 2000, period = 1000;
    for (unsigned long t = 1000; t < start + 20 * period + 2000; t += tick) {
      float a = 0.0f;
      for (int i = 0; i < 20; i++) {
        a += bump(t, start + i * period, 2.0f);
        a += bump(t, start + i * period + 300, 1.5f);
      }
      steps += d.update(G_OFFSET + a, t);
    }
    checkf(steps == 20, "tick %lums: %d steps for 20 double-bump steps", tick,
           steps);
  }
}

static void test_sustained_motion_needs_rearm() {
  // Held above threshold for a full second: one count, because the detector
  // only re-arms once the signal falls below half the threshold. A square
  // pulse, so the baseline can't drift fast enough to hide it.
  printf("a 1s plateau above threshold counts once\n");
  for (unsigned long tick : TICKS_MS) {
    StepDetector d = seeded(G_OFFSET, 1000);
    int steps = 0;
    for (unsigned long t = 1000; t < 5000; t += tick) {
      float a = (t >= 2000 && t < 3000) ? 3.0f : 0.0f;
      steps += d.update(G_OFFSET + a, t);
    }
    checkf(steps == 1, "tick %lums: plateau counted %d steps", tick, steps);
  }
}

int main() {
  printf("step_detector: threshold=%.2f debounce=%lums tau=%.0fms\n\n",
         STEP_THRESHOLD, STEP_DEBOUNCE_MS, BASELINE_TAU_MS);

  test_still_with_offset_counts_nothing();
  test_tilt_after_boot_settles();
  test_walk_counts_every_step();
  test_heel_and_push_off_count_once();
  test_sustained_motion_needs_rearm();

  printf("\n%d checks, %d failures\n", checks, failures);
  if (failures == 0) printf("PASS\n");
  else printf("FAIL\n");
  return failures == 0 ? 0 : 1;
}
