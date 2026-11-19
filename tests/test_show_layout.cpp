// Host tests for the totem's shows (totem/show_layout.h): the typed messages,
// the ticker spin and the scheduler. Catches a font edit that breaks a glyph,
// a typing script that doesn't end on the phrase, a frame drawn off the ring
// and a scheduler that repeats itself.

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
  // --- Font: every row of a glyph the same width, exactly H rows ---
  for (int i = 0; i < showfont::FONT_LEN; i++) {
    const showfont::Glyph &g = showfont::FONT[i];
    size_t w = strlen(g.rows[0]);
    CHECK(w > 0 && w <= 9, "glyph '%c' width %d", g.c, (int)w);
    for (int r = 0; r < showfont::H; r++)
      CHECK(g.rows[r] && strlen(g.rows[r]) == w, "glyph '%c' row %d is %d wide, not %d", g.c, r,
            g.rows[r] ? (int)strlen(g.rows[r]) : -1, (int)w);
  }
  for (char c = 'A'; c <= 'Z'; c++) CHECK(showfont::find(c), "font has no '%c'", c);
  for (char c = '0'; c <= '9'; c++) CHECK(showfont::find(c), "font has no '%c'", c);
  for (int i = 0; i < NPHRASES; i++)
    for (const char *p = PHRASES[i]; *p; p++)
      CHECK(showfont::find(showfont::upper(*p)), "phrase %d: font has no '%c'", i, *p);

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

  // --- Typing: many seeds, every phrase, typos on and off ---
  for (int i = 0; i < NPHRASES; i++)
    for (uint32_t seed = 1; seed < 200; seed++)
      for (int typo = 0; typo <= 1; typo++) {
        show::Typing ty;
        ty.build(PHRASES[i], seed * 2654435761u, SHOW_TYPING_LETTERS_PER_S, typo);
        uint32_t prev = 0;
        int backsBeforeDone = 0;
        for (int e = 0; e < ty.n; e++) {
          CHECK(ty.ev[e].t >= prev, "phrase %d seed %u: events out of order", i, seed);
          prev = ty.ev[e].t;
          if (ty.ev[e].t <= ty.typedAt && ty.ev[e].kind == show::BACK) backsBeforeDone++;
        }
        CHECK(typo ? (ty.typoAt >= 0 && backsBeforeDone >= 1 && backsBeforeDone <= 2) : backsBeforeDone == 0,
              "phrase %d seed %u typo %d: %d backspaces before the end", i, seed, typo, backsBeforeDone);
        show::LineState s;
        show::lineAt(ty, ty.typedAt, s);
        CHECK(strcmp(s.line, ty.text) == 0, "phrase %d seed %u: typed \"%s\", wanted \"%s\"", i, seed, s.line, ty.text);
        show::lineAt(ty, ty.emptyAt, s);
        CHECK(s.len == 0 && s.cursorX == 0, "phrase %d seed %u: not empty after the rewind", i, seed);
        CHECK(ty.endAt > ty.emptyAt && ty.endAt < 40000, "phrase %d seed %u: %u ms long", i, seed, ty.endAt);
        // Once the glide settles, the cursor is in its spot and the line starts no further right than LEFT.
        for (uint32_t t = show::TYPE_START_MS; t < ty.emptyAt; t += 37) {
          show::lineAt(ty, t, s);
          CHECK(s.view >= -0.01f, "phrase %d: view %f", i, s.view);
          float right = s.cursorX - s.view + show::LEFT + show::CURSOR_W - 1;
          CHECK(right <= show::CURSOR_RIGHT + show::MAX_LAG + 0.01f, "phrase %d t=%u: cursor off its half (%f)", i, t, right);
        }
      }
  // Typing speed: about the configured letters per second.
  {
    show::Typing ty;
    ty.build(PHRASES[0], 7, 3.5f, false);
    float secs = (ty.typedAt - show::TYPE_START_MS) / 1000.0f;
    float cps = (ty.len - 1) / secs;
    CHECK(cps > 2.5f && cps < 4.5f, "typing at %.2f letters/s, wanted about 3.5", cps);
  }

  // --- Phrase frames: text only on its rows, both halves the same, fades at the ends ---
  for (int i = 0; i < NPHRASES; i++) {
    pl.startPhrase(PHRASES[i], 99u + i, SHOW_TYPING_LETTERS_PER_S, true, show::rgb(255, 255, 255));
    for (uint32_t t = 0; t < pl.duration(); t += 33) {
      float mix = pl.render(t, normal, people, frame);
      CHECK(mix >= 0 && mix <= 1, "phrase %d t=%u: mix %f", i, t, mix);
      if (mix < 1) continue;
      for (int y = 0; y < show::ROWS; y++)
        for (int x = 0; x < show::COLS; x++) {
          bool lit = frame[y][x].r || frame[y][x].g || frame[y][x].b;
          if (lit) CHECK(y >= show::TEXT_Y && y < show::TEXT_Y + showfont::H, "phrase %d t=%u: pixel lit on row %d", i, t, y);
          if (x < show::HALF) CHECK(sameRgb(frame[y][x], frame[y][x + show::HALF]), "phrase %d t=%u: halves differ at %d,%d", i, t, x, y);
        }
    }
    // The fast per-frame column build matches the plain per-pixel lookup.
    for (uint32_t t = show::CURSOR_IN_MS; t < pl.typing.cursorOutAt; t += 97) {
      pl.render(t, normal, people, frame);
      show::LineState s;
      show::lineAt(pl.typing, t, s);
      bool cur = show::cursorOn(pl.typing, s, t);
      int vi = (int)floorf(s.view);
      float f = s.view - vi;
      for (int c = 0; c < show::HALF; c++)
        for (int y = 0; y < showfont::H; y++) {
          float a = (show::lineLit(s, cur, c - show::LEFT + vi, y) ? 1.0f - f : 0) +
                    (show::lineLit(s, cur, c - show::LEFT + vi + 1, y) ? f : 0);
          if (c < show::FADE_COLS) a *= (float)(c + 1) / (show::FADE_COLS + 1);
          int want = a <= 0.02f ? 0 : (int)(255 * (a > 1 ? 1 : a) + 0.5f);
          int got = frame[show::TEXT_Y + y][c].r;
          CHECK(got - want <= 1 && want - got <= 1, "phrase %d t=%u: pixel %d,%d is %d, want %d", i, t, c, y, got, want);
        }
    }
    pl.render(0, normal, people, frame);
    CHECK(sameRgb(frame[5][5], normal[5][5]), "phrase %d: first frame isn't the heart rate", i);
    pl.render(pl.duration() - 1, normal, people, frame);
    CHECK(frame[5][40].b > 60, "phrase %d: last frame hasn't faded the heart rate back in", i);
  }

  // --- Ticker: starts and ends on the faces, moves forward, stays on the ring ---
  for (int count = 1; count <= 2; count++) {
    people.count = count;
    pl.startTicker(people, SHOW_TICKER_PX_PER_S);
    CHECK(pl.ticker.len > 40 && pl.ticker.len < show::TICK_MAX, "ticker %d people: %d columns", count, pl.ticker.len);
    CHECK(pl.duration() > 6000 && pl.duration() < 30000, "ticker %d people: %u ms", count, pl.duration());
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
  for (int last = -1; last < 4; last++)
    for (uint32_t r = 0; r < 50; r++) {
      int a = show::pickNext(last, 4, true, r * 7919u);
      CHECK(a >= 0 && a < 4 && a != last, "pickNext(%d) gave %d", last, a);
      int b = show::pickNext(last, 4, false, r * 7919u);
      CHECK(b >= 1 && b < 4 && b != last, "pickNext(%d, no ticker) gave %d", last, b);
    }
  CHECK(show::pickNext(1, 2, false, 5) == 1, "only one phrase and no ticker: play it again");
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
