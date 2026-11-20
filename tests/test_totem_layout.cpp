// Host tests for the totem's 16x16 face layout and LED chain map.
// Pure geometry: catches a glyph or heart edit that spills off the panel or
// into its neighbour, and a PANEL_* flag change that loses or doubles an LED.

#include "../totem/face_layout.h"
#include "../totem/led_map.h"
#include "../totem/ring_layout.h"
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
  CHECK(heartPixels >= 30, "heart too small: %d px", heartPixels);   // 8x6 heart: 32
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
    int x0 = face::textX(g, n), x1 = x0 + face::textWidth(g, n) - 1;
    CHECK(x0 >= 0 && x1 < face::W, "bpm %d box %d..%d off panel", bpm, x0, x1);
    int slack = (x0) - (face::W - 1 - x1);   // left margin minus right margin
    CHECK(slack == 0 || slack == 1, "bpm %d not centred (%d)", bpm, slack);
  }
  { int g[3]; CHECK(face::bpmGlyphs(0, g) == 2 && g[0] == face::GLYPH_DASH, "0 bpm should read --"); }
  { int g[3]; CHECK(face::bpmGlyphs(200, g) == 2 && g[0] == face::GLYPH_DASH, "200 bpm would need a wide 2: should read --"); }
  // Glyph art: every row exactly one cell wide, so a typo can't shift a digit.
  for (int i = 0; i < 12; i++)
    for (int r = 0; r < face::DIGIT_H; r++)
      CHECK(strlen(face::GLYPH[i][r]) == (size_t)face::DIGIT_W, "glyph %d row %d is %d wide", i, r, (int)strlen(face::GLYPH[i][r]));
  // The narrow 1 only uses its first NARROW_W columns.
  for (int r = 0; r < face::DIGIT_H; r++)
    for (int c = face::NARROW_W; c < face::DIGIT_W; c++)
      CHECK(face::GLYPH[face::GLYPH_NARROW_ONE][r][c] == '.', "narrow 1 spills into column %d", c);
  { int g[3]; CHECK(face::bpmGlyphs(128, g) == 3 && g[0] == face::GLYPH_NARROW_ONE && g[1] == 2 && g[2] == 8, "128 digits"); }
  // Four faces side by side: the gap between two panels' numbers (16 - width)
  // must be clearly wider than the gap between digits, or neighbours run together.
  for (int bpm = 10; bpm <= 199; bpm++) {
    int g[3], n = face::bpmGlyphs(bpm, g);
    int seam = face::W - face::textWidth(g, n);
    CHECK(seam >= 3 * face::DIGIT_GAP, "bpm %d leaves only %d px between panels", bpm, seam);
  }

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

  // Ring map: every (col, row) of the 48x16 ring lands on a distinct LED.
  std::set<int> ringSeen;
  for (int c = 0; c < RING_COLS; c++)
    for (int r = 0; r < FACE_H; r++) {
      int i = ringLedIndex(c, r);
      CHECK(i >= 0 && i < NUM_LEDS, "ring %d,%d off the chain (%d)", c, r, i);
      ringSeen.insert(i);
    }
  CHECK((int)ringSeen.size() == NUM_LEDS, "ring map hits %d of %d LEDs", (int)ringSeen.size(), NUM_LEDS);
#if TOTEM_RING
  CHECK(RING_COLS == ring::COLS, "ring is %d columns, layout expects %d", RING_COLS, ring::COLS);
#endif

  // EKG: the whole wave stays in rows 0..8, clear of the ticker.
  for (int k = -2; k < ring::WAVE_LEN + 2; k++)
    for (int prev = -2; prev <= 6; prev++) {
      int row = ring::waveRow(ring::waveAt(k));
      CHECK(row >= 0 && row < ring::TEXT_Y - 1, "EKG step %d draws on row %d", k, row);
    }

  // Font: every glyph has 6 equal rows, A-Z all exist, nothing wider than 5.
  for (int i = 0; i < ring::FONT_LEN; i++) {
    int w = (int)strlen(ring::FONT[i].rows[0]);
    CHECK(w >= 1 && w <= 5, "glyph '%c' is %d wide", ring::FONT[i].c, w);
    for (int r = 1; r < ring::TEXT_H; r++)
      CHECK((int)strlen(ring::FONT[i].rows[r]) == w, "glyph '%c' row %d ragged", ring::FONT[i].c, r);
  }
  for (char c = 'A'; c <= 'Z'; c++) { const char *rows[ring::TEXT_H]; CHECK(ring::glyphRows(c, rows) > 0, "no glyph for %c", c); }
  for (const char *p = ring::SLOGAN_TEXT; *p; p++) { const char *rows[ring::TEXT_H]; CHECK(ring::glyphRows(*p, rows) > 0, "slogan char '%c' missing", *p); }

  // Ticker: you + 4 friends at 3-digit rates fit, each rate followed by the
  // slogan; friends who aren't live drop out; you show "--" when not connected.
  {
    static ring::Col cols[ring::MAX_COLS];
    ring::Person all[5] = {{'<', 128, true}, {'V', 101, true}, {'K', 133, true}, {'M', 99, true}, {'W', 150, true}};
    int full = ring::buildTicker(all, 5, cols, ring::MAX_COLS);
    CHECK(full > 0 && full < ring::MAX_COLS, "full ticker is %d cols (max %d)", full, ring::MAX_COLS);
    int rates = 0, slogans = 0; uint8_t last = ring::BLANK;
    for (int i = 0; i < full; i++) {
      if (cols[i].kind == ring::RATE && last != ring::RATE) rates++;
      if (cols[i].kind == ring::SLOGAN && last != ring::SLOGAN) slogans++;
      if (cols[i].kind != ring::BLANK) last = cols[i].kind;
    }
    CHECK(rates == 5 && slogans == 5, "full ticker has %d rates, %d slogans", rates, slogans);

    ring::Person some[3] = {{'<', 0, false}, {'V', 0, false}, {'K', 90, true}};
    int part = ring::buildTicker(some, 3, cols, ring::MAX_COLS);
    int people[5] = {0}; bool sawV = false;
    for (int i = 0; i < part; i++) if (cols[i].person >= 0) { people[cols[i].person]++; if (cols[i].person == 1) sawV = true; }
    CHECK(people[0] > 0 && people[2] > 0 && !sawV, "offline friend shown, or you/live friend missing");
    ring::Person solo[1] = {{'<', 72, true}};
    int one = ring::buildTicker(solo, 1, cols, ring::MAX_COLS);
    CHECK(one > 40 && one < 80, "solo ticker is %d cols", one);
  }

  // Bar fill: a BPM wobbling across a rounding point holds still, a clear
  // move goes through, and a jump of several pixels isn't held back.
  CHECK(face::barFill(1.49f, 2) == 2 && face::barFill(1.71f, 1) == 1, "bar fill flicks at a rounding point");
  CHECK(face::barFill(1.80f, 1) == 2 && face::barFill(1.20f, 2) == 1, "bar fill stuck after a clear move");
  CHECK(face::barFill(6.0f, 1) == 6 && face::barFill(0.4f, 5) == 0, "bar fill held back a big jump");
  {
    // 62-64 bpm alternating, as in the serial log: the end pixel never moves.
    int fill = face::barFill(barFraction(63) * face::W, 0), moves = 0;
    for (int i = 0; i < 40; i++) {
      int f = face::barFill(barFraction(i % 2 ? 62 : 63) * face::W, fill);
      if (f != fill) moves++;
      fill = f;
    }
    CHECK(moves == 0, "bar end pixel moved %d times with bpm 62/63", moves);
  }

  // Visible floor: at every global brightness, the floor survives FastLED's
  // v*(b+1)>>8 and one less doesn't (so it isn't brighter than it needs to be).
  for (int b = 1; b <= 255; b++) {
    int f = face::visibleFloor((uint8_t)b);
    CHECK(((f * (b + 1)) >> 8) >= 1, "floor %d goes dark at brightness %d", f, b);
    CHECK(f == 1 || (((f - 1) * (b + 1)) >> 8) == 0, "floor %d higher than needed at brightness %d", f, b);
  }
  CHECK(((10 * (14 + 1)) >> 8) == 0, "the blank-pixel case no longer reproduces: recheck the floor comment");

  std::printf("totem layout: %s\n", failures ? "FAILED" : "ok");
  return failures ? 1 : 0;
}
