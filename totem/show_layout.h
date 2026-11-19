#pragma once

// ============================================================================
// SHOWS — the every-few-minutes moments that break up the heart rate.
// Pure, no Arduino, no FastLED: tested under tests/, and the preview GIFs are
// drawn by running this same code on a laptop.
// ============================================================================
//
// Both shows draw onto the whole ring: 64 columns round (the four faces in
// chain order, see ringBlockPanel() in totem.ino) by 16 rows.
//
// TYPED MESSAGE (Above and Beyond style)
//   The heart rate fades out and a cursor blinks on a black screen. Then the
//   phrase is typed, one letter at a time, at a human pace with a little
//   jitter, in 10 px tall bold capitals. The ring is split into two halves,
//   each with its own cursor, opposite each other, so half the crowd sees the
//   letters land head-on. The line slides left away from the cursor like a
//   terminal scrolling sideways and dissolves at the far end of its half, so
//   anyone standing anywhere sees the words pass in order, never a jumble of
//   two copies side by side. Once per phrase it hits a neighbouring key,
//   sometimes types one more letter, notices, backspaces and fixes it. When
//   the phrase is done the cursor blinks for a couple of seconds, then the
//   whole line is held-backspaced away (it rewinds back through the phrase),
//   the cursor blinks once more and the heart rate fades back in.
//
// TICKER SPIN
//   The four faces start sliding round the ring, faster and faster, and
//   behind them comes a stock ticker: a pulsing heart, the BPM in its zone
//   colour, and a green up or red down arrow with how much it moved over the
//   last 10 minutes. Yours (red heart), then your friend's (cyan), twice
//   round. Then the faces come round again, slow down, bump into place and
//   the zone bars flash.

#include <stdint.h>
#include <math.h>
#include <string.h>
#include "face_layout.h"
#include "show_font.h"

namespace show {

const int COLS = 64, ROWS = 16, FACE = 16;
const int TEXT_Y = 3;            // big text on rows 3..12

typedef Rgb Frame[ROWS][COLS];

inline Rgb rgb(uint8_t r, uint8_t g, uint8_t b) { Rgb c; c.r = r; c.g = g; c.b = b; return c; }
inline Rgb scale(Rgb c, float k) {
  if (k <= 0) return rgb(0, 0, 0);
  if (k >= 1) return c;
  return rgb((uint8_t)(c.r * k + 0.5f), (uint8_t)(c.g * k + 0.5f), (uint8_t)(c.b * k + 0.5f));
}
inline Rgb mixc(Rgb a, Rgb b, float k) {   // a -> b
  if (k <= 0) return a;
  if (k >= 1) return b;
  return rgb((uint8_t)(a.r + (b.r - a.r) * k + 0.5f), (uint8_t)(a.g + (b.g - a.g) * k + 0.5f),
             (uint8_t)(a.b + (b.b - a.b) * k + 0.5f));
}

// Small deterministic random numbers (xorshift), so a seed always gives the
// same typing, and the host tests and GIFs match the totem.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  int range(int lo, int hi) { return lo + (int)(next() % (uint32_t)(hi - lo + 1)); }   // inclusive
  float unit() { return (next() & 0xFFFFFF) / 16777216.0f; }
};

// ---------------------------------------------------------------------------
// Typing
// ---------------------------------------------------------------------------
const int MAX_CHARS = 60;
const int MAX_EVENTS = 2 * MAX_CHARS + 12;
enum EvKind : uint8_t { TYPE = 0, BACK = 1 };
struct Event { uint32_t t; uint8_t kind; char c; };

// Message timeline, ms from the start of the show.
const uint32_t FADE_OUT_MS = 450;     // heart rate fades to black
const uint32_t CURSOR_IN_MS = 650;    // cursor appears
const uint32_t BLINK_MS = 400;        // cursor on 400, off 400 while idle
const uint32_t TYPE_START_MS = CURSOR_IN_MS + 4 * BLINK_MS;   // two blinks, then typing
const uint32_t SOLID_MS = 500;        // cursor stays solid this long after a key
const uint32_t HOLD_MS = 2400;        // finished phrase sits there, cursor blinking
const uint32_t REWIND_MS = 55;        // held backspace: one letter every 55 ms
const uint32_t AFTER_EMPTY_MS = 1300; // cursor blinks once on the empty line
const uint32_t BLACK_MS = 150;
const uint32_t FADE_IN_MS = 550;      // heart rate fades back in

// Each half of the ring (32 columns, two panels) shows the line:
//   cols 0..7    where the oldest letters dissolve (fade in from dark)
//   col  8       where the first letter lands
//   cols 25..26  where the cursor settles once the line is long
//   cols 27..31  room for the newest letter while the line glides over
const int HALF = 32;
const int CURSOR_W = 2;
const int LEFT = 8;
const int CURSOR_RIGHT = 26;
const int FADE_COLS = 8;
const float MAX_LAG = 6.0f;      // columns the glide may trail behind the cursor

// The QWERTY keys either side, for a believable typo.
inline char neighbourKey(char c, Rng &rng) {
  static const char *const ROWS_[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  for (int r = 0; r < 3; r++) {
    const char *row = ROWS_[r];
    int n = (int)strlen(row);
    for (int i = 0; i < n; i++)
      if (row[i] == c) {
        if (i == 0) return row[1];
        if (i == n - 1) return row[n - 2];
        return row[i + ((rng.next() & 1) ? 1 : -1)];
      }
  }
  return c == 'A' ? 'S' : 'A';
}

inline bool isLetter(char c) { return c >= 'A' && c <= 'Z'; }

// Width of a character plus the gap after it.
inline int advance(char c) {
  const showfont::Glyph *g = showfont::find(c);
  return g ? showfont::width(g) + showfont::GAP : 0;
}

struct Typing {
  char text[MAX_CHARS + 1];   // the phrase, uppercased, unknown characters dropped
  int len;
  Event ev[MAX_EVENTS];
  int n;
  int typoAt;                 // index of the letter that gets fumbled, -1 for none
  uint32_t typedAt;           // last letter of the phrase lands
  uint32_t emptyAt;           // last rewind backspace
  uint32_t cursorOutAt, fadeInAt, endAt;

  // cps: letters per second on average. typo: one fumble or none.
  void build(const char *phrase, uint32_t seed, float cps, bool typo) {
    Rng rng(seed);
    len = 0;
    for (int i = 0; phrase[i] && len < MAX_CHARS; i++) {
      char c = showfont::upper(phrase[i]);
      if (showfont::find(c) && !(c == ' ' && (len == 0 || text[len - 1] == ' '))) text[len++] = c;
    }
    while (len > 0 && text[len - 1] == ' ') len--;
    text[len] = 0;

    // The fumble lands mid-word, away from both ends of the phrase.
    typoAt = -1;
    if (typo) {
      int cand[MAX_CHARS], nc = 0;
      for (int i = 3; i < len - 2; i++)
        if (isLetter(text[i]) && isLetter(text[i - 1])) cand[nc++] = i;
      if (nc) typoAt = cand[rng.next() % nc];
    }

    if (cps < 1.0f) cps = 1.0f;
    const float base = 1000.0f / cps;
    n = 0;
    uint32_t t = TYPE_START_MS;
    for (int i = 0; i < len; i++) {
      if (i == typoAt) {
        ev[n++] = Event{t, TYPE, neighbourKey(text[i], rng)};
        int extra = (i + 1 < len && isLetter(text[i + 1]) && (rng.next() & 1)) ? 1 : 0;
        if (extra) { t += (uint32_t)(base * (0.7f + 0.4f * rng.unit())); ev[n++] = Event{t, TYPE, text[i + 1]}; }
        t += 520 + rng.range(0, 260);                  // the "oops" pause
        for (int k = 0; k <= extra; k++) { ev[n++] = Event{t, BACK, 0}; t += 170 + rng.range(0, 60); }
        t += 160;
      }
      ev[n++] = Event{t, TYPE, text[i]};
      float k = 0.65f + 0.7f * rng.unit();             // human jitter
      if (text[i] == ' ') k *= 0.8f;                   // the space bar comes quickly...
      if (i > 0 && text[i - 1] == ' ') k += 0.35f;     // ...the first letter of a word less so
      t += (uint32_t)(base * k);
    }
    typedAt = n ? ev[n - 1].t : TYPE_START_MS;
    t = typedAt + HOLD_MS;
    for (int i = 0; i < len && n < MAX_EVENTS; i++) { ev[n++] = Event{t, BACK, 0}; t += REWIND_MS; }
    emptyAt = n ? ev[n - 1].t : t;
    cursorOutAt = emptyAt + AFTER_EMPTY_MS;
    fadeInAt = cursorOutAt + BLACK_MS;
    endAt = fadeInAt + FADE_IN_MS;
  }
};

// The line as it stands at time t, and where the view has glided to.
struct LineState {
  char line[MAX_CHARS + 2];
  int len;
  int cursorX;        // text x of the cursor's left edge
  float view;         // text x shown at column LEFT
  uint32_t lastKey;   // time of the last event (or the cursor's arrival)
};

inline float viewTarget(int cursorX) {
  float v = (float)(cursorX + CURSOR_W - 1 - (CURSOR_RIGHT - LEFT));
  return v > 0 ? v : 0;
}

inline void lineAt(const Typing &ty, uint32_t t, LineState &s) {
  s.len = 0; s.cursorX = 0; s.view = 0; s.lastKey = CURSOR_IN_MS;
  float target = 0;
  uint32_t prev = 0;
  for (int i = 0; i < ty.n && ty.ev[i].t <= t; i++) {
    const Event &e = ty.ev[i];
    float tau = e.kind == BACK && i > 0 && ty.ev[i - 1].kind == BACK && e.t - ty.ev[i - 1].t < 100 ? 45.0f : 55.0f;
    s.view = target + (s.view - target) * expf(-(float)(e.t - prev) / tau);
    prev = e.t;
    if (e.kind == TYPE) {
      if (s.len < MAX_CHARS + 1) { s.line[s.len++] = e.c; s.cursorX += advance(e.c); }
    } else if (s.len > 0) {
      s.len--; s.cursorX -= advance(s.line[s.len]);
    }
    s.lastKey = e.t;
    target = viewTarget(s.cursorX);
  }
  s.line[s.len] = 0;
  s.view = target + (s.view - target) * expf(-(float)(t - prev) / 55.0f);
  // Two quick keys can leave the glide a whole letter behind; never let the
  // cursor run more than MAX_LAG columns past its spot (or the line that far
  // past its start when backspacing).
  if (s.view < target - MAX_LAG) s.view = target - MAX_LAG;
  if (s.view > target + MAX_LAG) s.view = target + MAX_LAG;
}

inline bool cursorOn(const Typing &ty, const LineState &s, uint32_t t) {
  if (t < CURSOR_IN_MS || t >= ty.cursorOutAt) return false;
  if (t < TYPE_START_MS) return ((t - CURSOR_IN_MS) / BLINK_MS) % 2 == 0;
  uint32_t since = t - s.lastKey;
  if (since < SOLID_MS) return true;
  return ((since - SOLID_MS) / BLINK_MS) % 2 == 1;   // off first, then on...
}

// Is text column x (cursor included), row y (0..9) lit?
inline bool lineLit(const LineState &s, bool cursor, int x, int y) {
  if (cursor && x >= s.cursorX && x < s.cursorX + CURSOR_W) return true;
  if (x < 0 || x >= s.cursorX) return false;
  int x0 = 0;
  for (int i = 0; i < s.len; i++) {
    const showfont::Glyph *g = showfont::find(s.line[i]);
    int w = showfont::width(g);
    if (x < x0 + w) return showfont::lit(g, x - x0, y);
    x0 += w + showfont::GAP;
    if (x < x0) return false;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Ticker
// ---------------------------------------------------------------------------
// Inputs for the ticker and its faces: index 0 is you, 1 your friend.
struct People {
  int count;            // 1, or 2 when a friend is connected
  int bpm[2];
  bool live[2];
  bool deltaKnown[2];
  int delta[2];         // BPM now minus 10 minutes ago (or as far back as known)
  Rgb zone[2];          // the BPM's zone colour
  Rgb heart[2];         // red for you, cyan for your friend
  float beat[2];        // heartbeat pulse 0..1, for the ticker's hearts
};

enum ColKind : uint8_t { K_BLANK = 0, K_HEART, K_BPM, K_UP, K_DOWN, K_FLAT };
struct TickCol { uint16_t bits; uint8_t kind; uint8_t person; };   // bit r = row r

const int TICK_MAX = 420;
const int TICK_LEAD = 8;        // dark columns after the faces, before the first heart
const int TICK_ENTRY_GAP = 14;  // between one person and the next

static const char *const ARROW_UP[4] = {"...X...", "..XXX..", ".XXXXX.", "XXXXXXX"};
static const char *const ARROW_DOWN[4] = {"XXXXXXX", ".XXXXX.", "..XXX..", "...X..."};
const int ARROW_Y = 7;          // rows 7..10, level with the middle of the digits
const int SMALL_Y = 6;          // 5x7 digits on rows 6..12, bottoms level with the big ones

// Ticker timeline: faces speed up, ticker passes at TICK_PX_PER_S, faces slow
// into place, a bump, the bars flash.
const uint32_t SPIN_UP_MS = 1500, SPIN_DOWN_MS = 1700, LOCK_MS = 450;

struct Ticker {
  TickCol col[TICK_MAX];
  int len;              // ticker columns between the two copies of the faces
  float pxPerS;
  uint32_t cruiseMs, endAt;

  int put(int n, uint16_t bits, uint8_t kind, uint8_t person) {
    if (n < TICK_MAX) col[n] = TickCol{bits, kind, person};
    return n + 1;
  }
  int gap(int n, int w) { for (int i = 0; i < w; i++) n = put(n, 0, K_BLANK, 0); return n; }
  int bigText(int n, const char *s, uint8_t kind, uint8_t person) {
    for (int i = 0; s[i]; i++) {
      const showfont::Glyph *g = showfont::find(s[i]);
      if (!g) continue;
      if (i) n = gap(n, showfont::GAP);
      for (int x = 0; x < showfont::width(g); x++) {
        uint16_t b = 0;
        for (int y = 0; y < showfont::H; y++)
          if (showfont::lit(g, x, y)) b |= (uint16_t)(1u << (TEXT_Y + y));
        n = put(n, b, kind, person);
      }
    }
    return n;
  }
  int smallText(int n, const char *s, uint8_t kind, uint8_t person) {
    for (int i = 0; s[i]; i++) {
      int gi = s[i] == '-' ? face::GLYPH_DASH : s[i] - '0';
      if (gi < 0 || gi > 10) continue;
      if (i) n = gap(n, 1);
      for (int x = 0; x < face::DIGIT_W; x++) {
        uint16_t b = 0;
        for (int y = 0; y < face::DIGIT_H; y++)
          if (face::GLYPH[gi][y][x] == 'X') b |= (uint16_t)(1u << (SMALL_Y + y));
        n = put(n, b, kind, person);
      }
    }
    return n;
  }
  int arrow(int n, const char *const rows[4], uint8_t kind, uint8_t person) {
    for (int x = 0; x < 7; x++) {
      uint16_t b = 0;
      for (int y = 0; y < 4; y++)
        if (rows[y][x] == 'X') b |= (uint16_t)(1u << (ARROW_Y + y));
      n = put(n, b, kind, person);
    }
    return n;
  }

  void build(const People &p, float pxPerS_) {
    pxPerS = pxPerS_ < 5 ? 5 : pxPerS_;
    int n = gap(0, TICK_LEAD);
    int reps = 2;
    for (int r = 0; r < reps; r++)
      for (int i = 0; i < p.count && i < 2; i++) {
        n = bigText(n, "#", K_HEART, (uint8_t)i);
        n = gap(n, 3);
        char num[8] = "--";
        if (p.live[i] && p.bpm[i] >= 10 && p.bpm[i] <= 999) {
          int v = p.bpm[i], k = v >= 100 ? 3 : 2;
          num[k] = 0;
          for (int j = k - 1; j >= 0; j--) { num[j] = (char)('0' + v % 10); v /= 10; }
        }
        n = bigText(n, num, K_BPM, (uint8_t)i);
        if (p.live[i] && p.deltaKnown[i]) {
          n = gap(n, 3);
          int d = p.delta[i];
          int a = d < 0 ? -d : d;
          if (a > 99) a = 99;
          if (d > 0) n = arrow(n, ARROW_UP, K_UP, (uint8_t)i);
          else if (d < 0) n = arrow(n, ARROW_DOWN, K_DOWN, (uint8_t)i);
          n = gap(n, d ? 1 : 0);
          char ds[4];
          if (a >= 10) { ds[0] = (char)('0' + a / 10); ds[1] = (char)('0' + a % 10); ds[2] = 0; }
          else { ds[0] = (char)('0' + a); ds[1] = 0; }
          n = smallText(n, ds, d > 0 ? K_UP : d < 0 ? K_DOWN : K_FLAT, (uint8_t)i);
        }
        n = gap(n, TICK_ENTRY_GAP);
      }
    len = n < TICK_MAX ? n : TICK_MAX;
    // Distance: the ticker plus one lap of the faces. Half of the speed-up
    // and slow-down time counts as travel at full speed.
    float dist = (float)(COLS + len);
    float rampPx = pxPerS * (SPIN_UP_MS + SPIN_DOWN_MS) / 2000.0f;
    float cruise = dist > rampPx ? (dist - rampPx) / pxPerS * 1000.0f : 0;
    cruiseMs = (uint32_t)cruise;
    endAt = SPIN_UP_MS + cruiseMs + SPIN_DOWN_MS + LOCK_MS;
  }

  float distance() const { return (float)(COLS + len); }

  // How far the ring has turned at t, in columns (fractional).
  float offset(uint32_t t) const {
    float v = pxPerS / 1000.0f;   // px per ms
    float D = distance();
    float up = SPIN_UP_MS, down = SPIN_DOWN_MS, cr = (float)cruiseMs;
    float ft = (float)t;
    float o;
    if (ft < up) o = 0.5f * v * ft * ft / up;
    else if (ft < up + cr) o = 0.5f * v * up + v * (ft - up);
    else if (ft < up + cr + down) {
      float u = ft - up - cr;
      o = 0.5f * v * up + v * cr + v * u - 0.5f * v * u * u / down;
    } else {
      o = D;
      // The bump: a small damped knock as it locks.
      float u = ft - up - cr - down;
      return D + 1.6f * expf(-u / 110.0f) * sinf(u / 38.0f);
    }
    // Rounding in cruiseMs: scale so the end lands exactly on D.
    float full = 0.5f * v * up + v * cr + 0.5f * v * down;
    return full > 0 ? o * D / full : o;
  }
};

// ---------------------------------------------------------------------------
// The player
// ---------------------------------------------------------------------------
enum Kind : uint8_t { NONE = 0, TICKER = 1, PHRASE = 2 };

struct Player {
  Kind kind = NONE;
  Typing typing;
  Ticker ticker;
  Rgb textRgb = {255, 255, 255};

  void startPhrase(const char *phrase, uint32_t seed, float cps, bool typo, Rgb text) {
    kind = PHRASE; textRgb = text;
    typing.build(phrase, seed, cps, typo);
  }
  void startTicker(const People &p, float pxPerS) {
    kind = TICKER;
    ticker.build(p, pxPerS);
  }
  uint32_t duration() const {
    return kind == PHRASE ? typing.endAt : kind == TICKER ? ticker.endAt : 0;
  }

  // Draws the show at t ms into out. normal is the heart rate display as the
  // ring would show it right now (live). Returns how much of the show's own
  // brightness to use: 0 = the heart rate's (fades, lock), 1 = the show's
  // (worked out once from its brightest frame), in between while the ticker
  // speeds up or slows down.
  float render(uint32_t t, const Frame &normal, const People &p, Frame &out) const {
    for (int y = 0; y < ROWS; y++)
      for (int x = 0; x < COLS; x++) out[y][x] = rgb(0, 0, 0);
    if (kind == PHRASE) return renderPhrase(t, normal, out);
    if (kind == TICKER) return renderTicker(t, normal, p, out);
    return 0;
  }

  float renderPhrase(uint32_t t, const Frame &normal, Frame &out) const {
    const Typing &ty = typing;
    if (t < FADE_OUT_MS) {
      float k = 1.0f - (float)t / FADE_OUT_MS;
      for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) out[y][x] = scale(normal[y][x], k * k);
      return 0;
    }
    if (t >= ty.fadeInAt) {
      float k = (float)(t - ty.fadeInAt) / FADE_IN_MS;
      for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) out[y][x] = scale(normal[y][x], k * k);
      return 0;
    }
    LineState s;
    lineAt(ty, t, s);
    bool cur = cursorOn(ty, s, t);
    // Both halves show the same 32 columns. Column c shows text x = c - LEFT +
    // view; a fractional view blends neighbouring columns so the glide is smooth.
    float fv = s.view;
    int vi = (int)floorf(fv);
    float f = fv - vi;
    // The lit rows of text columns x0 .. x0 + HALF, built once per frame
    // (looking each glyph up per pixel was too slow on the board).
    const int x0 = vi - LEFT;
    uint16_t bits[HALF + 1];
    for (int k = 0; k <= HALF; k++) bits[k] = 0;
    int gx = 0;
    for (int i = 0; i < s.len && gx <= x0 + HALF; i++) {
      const showfont::Glyph *g = showfont::find(s.line[i]);
      int w = showfont::width(g);
      for (int cx = 0; cx < w; cx++) {
        int k = gx + cx - x0;
        if (k < 0 || k > HALF) continue;
        for (int y = 0; y < showfont::H; y++)
          if (showfont::lit(g, cx, y)) bits[k] |= (uint16_t)(1u << y);
      }
      gx += w + showfont::GAP;
    }
    if (cur)
      for (int cx = 0; cx < CURSOR_W; cx++) {
        int k = s.cursorX + cx - x0;
        if (k >= 0 && k <= HALF) bits[k] = (uint16_t)((1u << showfont::H) - 1);
      }
    for (int c = 0; c < HALF; c++) {
      float fade = c < FADE_COLS ? (float)(c + 1) / (FADE_COLS + 1) : 1.0f;
      for (int y = 0; y < showfont::H; y++) {
        float a = ((bits[c] >> y) & 1 ? 1.0f - f : 0) + ((bits[c + 1] >> y) & 1 ? f : 0);
        a *= fade;
        if (a <= 0.02f) continue;
        Rgb px = scale(textRgb, a);
        for (int h = 0; h < COLS / HALF; h++) out[TEXT_Y + y][h * HALF + c] = px;
      }
    }
    return 1;
  }

  Rgb tickColour(const TickCol &tc, const People &p) const {
    int i = tc.person < 2 ? tc.person : 0;
    switch (tc.kind) {
      case K_HEART: {
        if (!p.live[i]) return rgb(70, 70, 70);
        return scale(p.heart[i], 0.35f + 0.65f * p.beat[i]);
      }
      case K_BPM: return p.live[i] ? p.zone[i] : rgb(70, 70, 70);
      case K_UP: return rgb(40, 255, 60);
      case K_DOWN: return rgb(255, 35, 35);
      case K_FLAT: return rgb(120, 120, 120);
      default: return rgb(0, 0, 0);
    }
  }

  // Strip column s: the faces, then the ticker, then the faces again.
  Rgb stripAt(int s, int y, const Frame &normal, const People &p) const {
    int L = ticker.len;
    if (s < 0) s = 0;
    if (s < COLS) return normal[y][s];
    if (s < COLS + L) {
      const TickCol &tc = ticker.col[s - COLS];
      return (tc.bits >> y) & 1 ? tickColour(tc, p) : rgb(0, 0, 0);
    }
    s -= COLS + L;
    return normal[y][s % COLS];
  }

  float renderTicker(uint32_t t, const Frame &normal, const People &p, Frame &out) const {
    float o = ticker.offset(t);
    int oi = (int)floorf(o);
    float f = o - oi;
    for (int c = 0; c < COLS; c++)
      for (int y = 0; y < ROWS; y++) {
        int s = c + oi;
        // The bump can swing a pixel past the end: wrap that into the faces.
        out[y][c] = mixc(stripAt(s, y, normal, p), stripAt(s + 1, y, normal, p), f);
      }
    uint32_t up = SPIN_UP_MS, down0 = SPIN_UP_MS + ticker.cruiseMs, lock = down0 + SPIN_DOWN_MS;
    if (t >= lock) {
      // The zone bars (row 0) flash white as it locks.
      float k = 1.0f - (float)(t - lock) / LOCK_MS;
      for (int c = 0; c < COLS; c++) out[0][c] = mixc(out[0][c], rgb(255, 255, 255), k * k * 0.85f);
      return 0;
    }
    if (t < up) return (float)t / up;
    if (t < down0) return 1;
    return 1.0f - (float)(t - down0) / SPIN_DOWN_MS;
  }
};

// ---------------------------------------------------------------------------
// Choosing the next show, and heart rate history for the ticker's arrows
// ---------------------------------------------------------------------------
// Programs are numbered 0 = ticker, 1..phrases = the phrases in order.

// Test mode: every program in turn.
inline int nextInOrder(int last, int count) { return count <= 0 ? -1 : (last + 1) % count; }

// Festival mode: any program but the last one; the ticker only when your
// heart rate is coming in. rnd is any random number.
inline int pickNext(int last, int count, bool tickerOk, uint32_t rnd) {
  int ok[32], n = 0;
  for (int i = 0; i < count && n < 32; i++) {
    if (i == 0 && !tickerOk) continue;
    if (i == last) continue;
    ok[n++] = i;
  }
  if (n == 0) return (last >= 0 && last < count && (last != 0 || tickerOk)) ? last : -1;
  return ok[rnd % (uint32_t)n];
}

// One sample every SAMPLE_MS; the oldest is about 10 minutes back.
struct HrHistory {
  static const int SLOTS = 31;
  static const uint32_t SAMPLE_MS = 20000;
  int16_t s[SLOTS];
  int n = 0, head = 0;
  void push(int bpm) { s[head] = (int16_t)bpm; head = (head + 1) % SLOTS; if (n < SLOTS) n++; }
  // Now minus the oldest sample that had a heart rate (needs a minute of history).
  bool delta(int now, int &d) const {
    if (now <= 0) return false;
    for (int k = n; k >= 4; k--) {    // k samples back; 4 = about a minute
      int v = s[(head - k + SLOTS * 2) % SLOTS];
      if (v > 0) { d = now - v; return true; }
    }
    return false;
  }
};

} // namespace show
