// Host tests for the totem's 16x16 face layout and LED chain map.
// Pure geometry: catches a glyph or heart edit that spills off the panel or
// into its neighbour, and a PANEL_* flag change that loses or doubles an LED.

#include "../totem/face_layout.h"
#include "../totem/led_map.h"
#include "../hr_zones.h"

#include <cstdio>
#include <set>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
  // Heart: inside rows 2..8, never on the bar row or the gap under it, never in the digit rows.
  int heartPixels = 0;
  for (int y = 0; y < face::H; y++)
    for (int x = 0; x < face::W; x++)
      if (face::heartAt(x, y)) {
        heartPixels++;
        CHECK(y > face::BAR_Y + 1, "heart on bar row or its gap at %d,%d", x, y);
        CHECK(y < face::DIGIT_Y - 1, "heart touches digits at %d,%d", x, y);
      }
  CHECK(heartPixels > 40, "heart too small: %d px", heartPixels);
  // Symmetric about the face's centre line, so it doesn't lean.
  for (int y = 0; y < face::H; y++)
    for (int x = 0; x < face::W; x++)
      CHECK(face::heartAt(x, y) == face::heartAt(face::W - 1 - x, y), "heart asymmetric at %d,%d", x, y);

  // Every plausible BPM fits on the panel, below the heart, and is centred.
  for (int bpm = 0; bpm <= 250; bpm++) {
    int g[3];
    int n = face::bpmGlyphs(bpm, g);
    int minx = 99, maxx = -1, lit = 0;
    for (int y = 0; y < face::H; y++)
      for (int x = 0; x < face::W; x++)
        if (face::textAt(g, n, x, y)) {
          lit++;
          CHECK(y >= face::DIGIT_Y && y < face::H, "bpm %d text off rows at y=%d", bpm, y);
          CHECK(!face::heartAt(x, y), "bpm %d text overlaps heart", bpm);
          if (x < minx) minx = x;
          if (x > maxx) maxx = x;
        }
    CHECK(lit > 0, "bpm %d draws nothing", bpm);
    int x0 = face::textX(n), x1 = x0 + face::textWidth(n) - 1;
    CHECK(x0 >= 0 && x1 < face::W, "bpm %d box %d..%d off panel", bpm, x0, x1);
    int slack = (x0) - (face::W - 1 - x1);   // left margin minus right margin
    CHECK(slack == 0 || slack == 1, "bpm %d not centred (%d)", bpm, slack);
  }
  { int g[3]; CHECK(face::bpmGlyphs(0, g) == 2 && g[0] == 10, "0 bpm should read --"); }
  { int g[3]; CHECK(face::bpmGlyphs(128, g) == 3 && g[0] == 1 && g[1] == 2 && g[2] == 8, "128 digits"); }

  // Zone bar on 16 px: slices tile the row exactly, fill never shrinks as BPM rises.
  CHECK(zoneSliceX(0, 16) == 0 && zoneSliceX(ZONE_COUNT, 16) == 16, "bar slices don't tile 16 px");
  int prev = -1;
  for (int bpm = REST_BPM; bpm <= ZONE_MAX_BPM + 10; bpm++) {
    int fill = (int)(barFraction(bpm) * 16 + 0.5f);
    CHECK(fill >= prev, "bar shrank at %d bpm", bpm);
    CHECK(fill <= 16, "bar overflows at %d bpm", bpm);
    prev = fill;
  }

  // LED map: every (face, x, y) lands on a distinct LED, all NUM_LEDS used.
  std::set<int> seen;
  for (int f = 0; f < FACE_COUNT; f++)
    for (int y = 0; y < FACE_H; y++)
      for (int x = 0; x < FACE_W; x++) {
        int i = ledIndex(f, x, y);
        CHECK(i >= f * LEDS_PER_FACE && i < (f + 1) * LEDS_PER_FACE, "face %d pixel %d,%d left its panel (%d)", f, x, y, i);
        seen.insert(i);
      }
  CHECK((int)seen.size() == NUM_LEDS, "LED map hits %d of %d LEDs", (int)seen.size(), NUM_LEDS);

  std::printf("totem layout: %s\n", failures ? "FAILED" : "ok");
  return failures ? 1 : 0;
}
