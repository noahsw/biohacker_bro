// Host tests for the totem's shows (totem/show_layout.h): the scrolling
// messages, the spin and the scheduler. Catches a font edit that breaks a
// glyph, a message that doesn't scroll steadily or lights the seam, a frame
// drawn off the ring and a scheduler that repeats itself.

#include "../totem/show_layout.h"
#include "../totem/totem_config.h"

#include <cstdio>
#include <cstring>
#include <cmath>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static const char *const PHRASES[] = {SHOW_PHRASES};
static const int NPHRASES = sizeof(PHRASES) / sizeof(PHRASES[0]);

static show::Frame normal, frame;

static bool sameRgb(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

int main() {
  // --- Fonts: every row of a glyph the same width, exactly h rows ---
  CHECK(show::PHRASE_FONT.h == show::PH, "PH is %d, the phrase font is %d tall", show::PH, show::PHRASE_FONT.h);
  const showfont::Font *fonts[2] = {&showfont::BOLD, &showfont::LIGHT};
  for (int fi = 0; fi < 2; fi++) {
    const showfont::Font &f = *fonts[fi];
    for (int i = 0; i < f.count; i++) {
      const showfont::Glyph &g = f.glyphs[i];
      size_t w = strlen(g.rows[0]);
      CHECK(w > 0 && w <= 9, "font %d glyph '%c' width %d", fi, g.c, (int)w);
      for (int r = 0; r < f.h; r++)
        CHECK(g.rows[r] && strlen(g.rows[r]) == w, "font %d glyph '%c' row %d is %d wide, not %d", fi, g.c, r,
              g.rows[r] ? (int)strlen(g.rows[r]) : -1, (int)w);
      for (int r = f.h; r < showfont::MAX_H; r++) CHECK(!g.rows[r], "font %d glyph '%c' has a row %d", fi, g.c, r);
    }
    for (char c = 'A'; c <= 'Z'; c++) CHECK(showfont::find(f, c), "font %d has no '%c'", fi, c);
    for (char c = '0'; c <= '9'; c++) CHECK(showfont::find(f, c), "font %d has no '%c'", fi, c);
  }
  CHECK(showfont::find(showfont::BOLD, '#'), "bold font has no heart");
  for (int i = 0; i < NPHRASES; i++)
    for (const char *p = PHRASES[i]; *p; p++)
      CHECK(showfont::find(show::PHRASE_FONT, showfont::upper(*p)), "phrase %d: font has no '%c'", i, *p);

  for (int y = 0; y < show::ROWS; y++)
    for (int x = 0; x < show::COLS; x++) normal[y][x] = show::rgb((uint8_t)(x * 3), (uint8_t)(y * 9), 77);
  show::People people{};
  people.count = 2;
  people.bpm[0] = 128; people.bpm[1] = 104; people.live[0] = people.live[1] = true;
  people.deltaKnown[0] = true; people.delta[0] = 12; people.deltaKnown[1] = true; people.delta[1] = -4;
  people.zone[0] = people.zone[1] = show::rgb(255, 200, 0);
  people.heart[0] = show::rgb(255, 20, 20); people.heart[1] = show::rgb(0, 200, 255);
  people.beat[0] = people.beat[1] = 1;

  static show::Player pl;

  // --- Messages: the text, the timeline, a steady scroll ---
  for (int i = 0; i < NPHRASES; i++)
    for (int reps = 1; reps <= 3; reps++) {
      show::Message m;
      m.build(PHRASES[i], SHOW_SCROLL_PX_PER_S, reps);
      char want[show::MAX_CHARS + 1];
      int n = 0;
      for (const char *q = PHRASES[i]; *q && n < show::MAX_CHARS; q++) want[n++] = showfont::upper(*q);
      want[n] = 0;
      CHECK(strcmp(m.text, want) == 0, "phrase %d: text \"%s\", wanted \"%s\"", i, m.text, want);
      CHECK(m.width > 20, "phrase %d: %d columns wide", i, m.width);
      CHECK(m.stripWidth() == reps * m.width + (reps - 1) * show::REPEAT_GAP, "phrase %d: strip width", i);
      CHECK(m.endAt < 150000, "phrase %d x%d: %u ms long", i, reps, m.endAt);
      CHECK(m.offset(0) == -show::DARK_FROM && m.offset(m.scrolledAt) == m.stripWidth(),
            "phrase %d: scroll runs %d..%d, wanted %d..%d", i, m.offset(0), m.offset(m.scrolledAt), -show::DARK_FROM, m.stripWidth());
      // One column at a time, evenly spaced, never back.
      int last = m.offset(0);
      uint32_t lastStep = 0, minGap = 1u << 30, maxGap = 0;
      int steps = 0;
      for (uint32_t t = show::SCROLL_AT_MS; t <= m.scrolledAt; t++) {
        int o = m.offset(t);
        CHECK(o == last || o == last + 1, "phrase %d t=%u: scroll jumped %d -> %d", i, t, last, o);
        if (o != last) {
          if (steps > 1) { uint32_t g = t - lastStep; if (g < minGap) minGap = g; if (g > maxGap) maxGap = g; }
          lastStep = t; steps++;
        }
        last = o;
      }
      CHECK(maxGap - minGap <= 2, "phrase %d: steps %u..%u ms apart", i, minGap, maxGap);
      // The copies: each one the phrase, a gap of REPEAT_GAP dark columns after it.
      for (int x = 0; x < m.stripWidth(); x++) {
        int k = x % m.period();
        for (int y = 0; y < show::PH; y++) {
          bool lit = show::messageLit(m, x, y);
          if (k >= m.width) CHECK(!lit, "phrase %d: lit in the gap at %d,%d", i, x, y);
          else CHECK(lit == show::messageLit(m, k, y), "phrase %d: copy differs at %d,%d", i, x, y);
        }
      }
    }

  // --- Message frames: text only on its rows, never in the dark seam, and
  // the fast column build matches the per-pixel lookup ---
  for (int i = 0; i < NPHRASES; i++) {
    pl.startPhrase(PHRASES[i], SHOW_SCROLL_PX_PER_S, show::rgb(255, 255, 255), SHOW_REPEATS);
    const show::Message &m = pl.message;
    bool sawFull = false;
    for (uint32_t t = 0; t < pl.duration(); t += 33) {
      float mix = pl.render(t, normal, people, frame);
      CHECK(mix >= 0 && mix <= 1, "phrase %d t=%u: mix %f", i, t, mix);
      if (mix < 1) continue;
      int off = m.offset(t);
      for (int y = 0; y < show::ROWS; y++)
        for (int x = 0; x < show::COLS; x++) {
          int v = (x - show::RING_AT_V0 + show::COLS) % show::COLS;
          bool inText = y >= show::PHRASE_Y && y < show::PHRASE_Y + show::PH;
          float a = inText && show::messageLit(m, v + off, y - show::PHRASE_Y) ? show::columnFade(v) : 0;
          int want = a <= 0.02f ? 0 : (int)(255 * a + 0.5f);
          int got = frame[y][x].r;
          CHECK(got - want <= 1 && want - got <= 1, "phrase %d t=%u: pixel %d,%d is %d, want %d", i, t, x, y, got, want);
          if (got) CHECK(show::columnFade(v) > 0, "phrase %d t=%u: lit in the dark seam at %d,%d", i, t, x, y);
          if (got >= 250) sawFull = true;
        }
    }
    CHECK(sawFull, "phrase %d: never at full brightness", i);
    pl.render(0, normal, people, frame);
    CHECK(sameRgb(frame[5][5], normal[5][5]), "phrase %d: first frame isn't the heart rate", i);
    pl.render(pl.duration() - 1, normal, people, frame);
    CHECK(frame[5][40].b > 60, "phrase %d: last frame hasn't faded the heart rate back in", i);
    pl.render(m.scrolledAt + 10, normal, people, frame);
    bool dark = true;
    for (int y = 0; y < show::ROWS; y++)
      for (int x = 0; x < show::COLS; x++) if (frame[y][x].r) dark = false;
    CHECK(dark, "phrase %d: text left over once it has scrolled through", i);
    // The brightness scan's shortcut: no frame after scanEnd() is brighter
    // than the brightest before it.
    uint32_t best = 0, later = 0;
    for (uint32_t t = 0; t < pl.duration(); t += 80) {
      if (pl.render(t, normal, people, frame) <= 0) continue;
      uint32_t sum = 0;
      for (int y = 0; y < show::ROWS; y++)
        for (int x = 0; x < show::COLS; x++) sum += frame[y][x].r;
      if (t < pl.scanEnd()) { if (sum > best) best = sum; }
      else if (sum > later) later = sum;
    }
    CHECK(later <= best, "phrase %d: a frame after scanEnd() is brighter (%u > %u)", i, later, best);
  }

  // --- Ticker: starts and ends on the faces, moves forward, stays on the ring ---
  for (int opt = 0; opt < 4; opt++) {
    const int count = 1 + (opt & 1), numbers = opt >> 1;
    people.count = count;
    pl.startTicker(people, SHOW_TICKER_PX_PER_S, numbers, SHOW_SPIN_LAPS);
    if (numbers) CHECK(pl.ticker.len > 40 && pl.ticker.len < show::TICK_MAX, "ticker %d people: %d columns", count, pl.ticker.len);
    else CHECK(pl.ticker.len == 0, "faces-only spin has %d ticker columns", pl.ticker.len);
    CHECK(pl.duration() > 3000 && pl.duration() < 30000, "ticker %d people numbers %d: %u ms", count, numbers, pl.duration());
    // Faces only: every frame is the faces, shifted round by whole columns.
    if (!numbers)
      for (uint32_t t = 0; t < pl.duration(); t += 41) {
        pl.render(t, normal, people, frame);
        int sh = -1;
        for (int k = 0; k < show::COLS && sh < 0; k++)
          if (sameRgb(frame[7][0], normal[7][k]) && sameRgb(frame[7][1], normal[7][(k + 1) % show::COLS])) sh = k;
        CHECK(sh >= 0, "faces-only spin at %u ms isn't the faces", t);
        for (int y = 1; y < show::ROWS && sh >= 0; y++)   // row 0 flashes at the lock
          for (int x = 0; x < show::COLS; x++)
            CHECK(sameRgb(frame[y][x], normal[y][(x + sh) % show::COLS]), "spin at %u ms: %d,%d blended", t, x, y);
      }
    float prev = -1;
    uint32_t lockAt = show::SPIN_UP_MS + pl.ticker.cruiseMs + show::SPIN_DOWN_MS;
    for (uint32_t t = 0; t < lockAt; t += 20) {
      float o = pl.ticker.offset(t);
      CHECK(o >= prev - 1e-3f, "ticker goes backwards at %u ms", t);
      prev = o;
    }
    CHECK(prev > pl.ticker.distance() - 0.5f, "ticker stops short: %f of %f", prev, pl.ticker.distance());
    pl.render(0, normal, people, frame);
    CHECK(sameRgb(frame[7][9], normal[7][9]), "ticker doesn't start on the faces");
    pl.render(pl.duration() - 1, normal, people, frame);
    CHECK(sameRgb(frame[7][9], normal[7][9]), "ticker doesn't end on the faces");
  }

  // --- Scheduler ---
  // Festival mode: spin, 1, spin, 2, spin, 3, spin, 1, ...
  {
    const int count = 4;
    int last = -1, lastPhrase = 0;
    const int want[] = {0, 1, 0, 2, 0, 3, 0, 1, 0, 2};
    for (int k = 0; k < 10; k++) {
      int p = show::nextAlternating(last, lastPhrase, count, true);
      CHECK(p == want[k], "alternating show %d: got %d, wanted %d", k, p, want[k]);
      last = p; if (p > 0) lastPhrase = p;
    }
    // No spin (or it can't play): the phrases in turn, picking up where they were.
    CHECK(show::nextAlternating(0, 2, count, false) == 3, "no spin after phrase 2: phrase 3");
    CHECK(show::nextAlternating(3, 3, count, false) == 1, "no spin after phrase 3: phrase 1");
    CHECK(show::nextAlternating(-1, 0, 1, true) == 0, "no phrases: spin");
    CHECK(show::nextAlternating(-1, 0, 1, false) == -1, "no phrases, no spin: nothing");
    CHECK(show::nextAlternating(1, 1, 2, true) == 0 && show::nextAlternating(0, 1, 2, true) == 1, "one phrase: spin, phrase");
  }
  { int p = -1, seen = 0; for (int k = 0; k < 8; k++) { p = show::nextInOrder(p, 4); seen |= 1 << p; } CHECK(seen == 15, "test mode skips a show"); }

  // --- Heart rate history ---
  {
    show::HrHistory h;
    int d = 0;
    CHECK(!h.delta(100, d), "delta with no history");
    for (int k = 0; k < 3; k++) h.push(90);
    CHECK(!h.delta(100, d), "delta before a minute of history");
    h.push(90);
    CHECK(h.delta(100, d) && d == 10, "delta after a minute: %d", d);
    for (int k = 0; k < 40; k++) h.push(80 + k);
    CHECK(h.delta(130, d) && d == 130 - (80 + 40 - show::HrHistory::SLOTS), "delta after 10+ minutes: %d", d);
    CHECK(!h.delta(0, d), "delta with no heart rate now");
  }

  if (failures) { std::printf("test_show_layout: %d failure(s)\n", failures); return 1; }
  std::printf("test_show_layout: all passed\n");
  return 0;
}
