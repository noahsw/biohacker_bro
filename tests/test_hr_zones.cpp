// ============================================================================
// Tests for the HR zone math (hr_zones.cpp)
// ============================================================================
//
// Run with `make -C tests`. No framework on purpose: this is ~4 functions of
// integer and float arithmetic, and a dependency you have to install would
// make the tests less likely to actually get run.
//
// These are deliberately PROPERTY tests, not a table of expected values for
// the current thresholds. The thresholds in config.h are wearer-specific and
// expected to be retuned after real use — a test that hardcodes "110bpm is
// zone 3" would fail on every retune and teach you to ignore it. What must
// hold for ANY sane threshold set is: the slices tile the panel exactly, the
// bar never goes backwards as BPM rises, zone boundaries land on slice edges,
// and the ends clamp.

#include "../hr_zones.h"

#include <cstdarg>
#include <cstdio>
#include <cmath>

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char *what) {
  checks++;
  if (!ok) {
    failures++;
    printf("  FAIL: %s\n", what);
  }
}

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

// --- The slices must tile the bar exactly: no gap, no overlap, nothing lost
// to integer rounding. 64/5 is not a whole number, so this is a real risk.
static void test_slices_tile_the_panel() {
  printf("slices tile the panel exactly\n");
  const int widths[] = {64, 32, 128, 5, 7};
  for (int w : widths) {
    checkf(zoneSliceX(0, w) == 0, "zone 0 must start at x=0 (width %d)", w);
    checkf(zoneSliceX(ZONE_COUNT, w) == w,
           "the last slice must end exactly at the panel edge (width %d)", w);
    for (int z = 0; z < ZONE_COUNT; z++) {
      int x0 = zoneSliceX(z, w);
      int x1 = zoneSliceX(z + 1, w);
      checkf(x1 >= x0, "slice %d must not run backwards (width %d)", z, w);
      // Each slice is within 1px of an even share — that's the most integer
      // division can distort it, and more than that means a bug not rounding.
      float even = (float)w / ZONE_COUNT;
      checkf(std::fabs((x1 - x0) - even) <= 1.0f,
             "slice %d is %dpx, expected within 1px of %.1f (width %d)",
             z, x1 - x0, even, w);
    }
  }
}

// --- The bar must never shrink as heart rate rises. This is the property a
// broken retune would most likely violate (e.g. thresholds out of order), and
// the one whose absence would be least visible on the panel.
static void test_bar_is_monotonic() {
  printf("bar never goes backwards as BPM rises\n");
  float previous = -1.0f;
  for (int bpm = 0; bpm <= 250; bpm++) {
    float f = barFraction(bpm);
    checkf(f >= previous, "barFraction(%d)=%.4f is less than at %d bpm (%.4f)",
           bpm, f, bpm - 1, previous);
    checkf(f >= 0.0f && f <= 1.0f, "barFraction(%d)=%.4f is outside 0..1", bpm, f);
    previous = f;
  }
}

// --- Crossing into a zone must land exactly on that zone's slice boundary,
// otherwise the bar's tip and the legend underneath disagree about where the
// zone starts — which is the whole point of drawing them together.
static void test_zone_boundaries_land_on_slice_edges() {
  printf("zone boundaries land on slice edges\n");
  for (int z = 0; z < ZONE_COUNT; z++) {
    int floorBpm = zoneFloorBpm(z);
    checkf(hrZone(floorBpm) == z,
           "%d bpm should be zone %d, got %d", floorBpm, z, hrZone(floorBpm));
    float f = barFraction(floorBpm);
    float expected = (float)z / ZONE_COUNT;
    checkf(std::fabs(f - expected) < 0.0001f,
           "at zone %d's first BPM (%d) the bar should be exactly %.2f, got %.4f",
           z, floorBpm, expected, f);
  }
}

// --- Each zone must be non-empty and correctly ordered. A retune that puts
// two thresholds in the wrong order would make a zone unreachable.
static void test_zones_are_ordered_and_non_empty() {
  printf("zones are ordered and none is empty\n");
  for (int z = 0; z < ZONE_COUNT; z++) {
    checkf(zoneFloorBpm(z + 1) > zoneFloorBpm(z),
           "zone %d is empty or inverted: floor %d, next floor %d",
           z, zoneFloorBpm(z), zoneFloorBpm(z + 1));
  }
  // Every zone must be reachable by some heart rate.
  bool seen[ZONE_COUNT] = {false};
  for (int bpm = 0; bpm <= 250; bpm++) seen[hrZone(bpm)] = true;
  for (int z = 0; z < ZONE_COUNT; z++) {
    checkf(seen[z], "zone %d is unreachable at any BPM", z);
  }
}

// --- Below resting the bar is empty, above the ceiling it's full. Without
// the clamps a resting reading below REST_BPM would produce a negative width.
static void test_ends_clamp() {
  printf("below resting clamps empty, above the ceiling clamps full\n");
  check(barFraction(0) == 0.0f, "0 bpm must give an empty bar");
  check(barFraction(REST_BPM) == 0.0f, "resting BPM must give an empty bar");
  check(barFraction(REST_BPM - 20) == 0.0f, "below resting must give an empty bar");
  check(hrZone(0) == 0, "0 bpm must be zone 0");
  check(barFraction(ZONE_MAX_BPM) == 1.0f, "the ceiling BPM must fill the bar");
  check(barFraction(ZONE_MAX_BPM + 60) == 1.0f, "above the ceiling must stay full");
  check(hrZone(ZONE_MAX_BPM + 60) == ZONE_COUNT - 1,
        "above the ceiling must stay in the top zone");
}

// --- The rendered pixel width must stay on the panel. This mirrors exactly
// what drawMainScreen does with barFraction's result.
static void test_rendered_width_stays_on_panel() {
  printf("rendered bar width stays within the panel\n");
  for (int bpm = 0; bpm <= 250; bpm++) {
    int width = (int)(barFraction(bpm) * PANEL_WIDTH + 0.5f);
    checkf(width >= 0 && width <= PANEL_WIDTH,
           "at %d bpm the bar is %dpx, outside 0..%d", bpm, width, PANEL_WIDTH);
  }
}

int main() {
  printf("hr_zones: REST=%d Z1=%d Z2=%d Z3=%d Z4=%d MAX=%d\n\n",
         REST_BPM, ZONE1_BPM, ZONE2_BPM, ZONE3_BPM, ZONE4_BPM, ZONE_MAX_BPM);

  test_slices_tile_the_panel();
  test_bar_is_monotonic();
  test_zone_boundaries_land_on_slice_edges();
  test_zones_are_ordered_and_non_empty();
  test_ends_clamp();
  test_rendered_width_stays_on_panel();

  printf("\n%d checks, %d failures\n", checks, failures);
  if (failures == 0) printf("PASS\n");
  else printf("FAIL\n");
  return failures == 0 ? 0 : 1;
}
