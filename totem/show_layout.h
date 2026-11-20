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
// SCROLLING MESSAGE
//   The heart rate fades out, then a phrase in 7 px tall thin capitals
//   (showfont::LIGHT) scrolls the whole way round the ring at one steady
//   speed, in whole-pixel steps (a sub-pixel blend looked smeared behind the
//   diffuser). Block 3 is the seam: the letters fade in out of it on one
//   side, its middle stays dark so people can tell where the line starts, and
//   they fade out into its other side. It comes round a few times (repeats),
//   a panel's width apart, so the crowd gets more than one go at reading it.
//   Once the last letter has gone the heart rate fades back in.
//
// TICKER SPIN
//   The four faces start sliding round the ring, faster and faster, go round
//   a lap or two, slow down, bump into place and the zone bars flash.
//   Optionally (numbers, off by default) a stock ticker comes round behind
//   them first: a pulsing heart, the BPM in its zone colour, and a green up or
//   red down arrow with how much it moved over the last 10 minutes. Yours
//   (hot pink heart), then your friend's (purple), twice round.

#include <stdint.h>
#include <math.h>
#include <string.h>
#include "face_layout.h"
#include "show_font.h"

namespace show {

const int COLS = 64, ROWS = 16, FACE = 16;
const int TEXT_Y = 3;            // ticker's bold text on rows 3..12

// The messages' font, centred: rows 4..10.
static const showfont::Font &PHRASE_FONT = showfont::LIGHT;
const int PH = 7;                // PHRASE_FONT.h, as a constant for array sizes
const int PHRASE_Y = (16 - PH) / 2;

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

// ---------------------------------------------------------------------------
// Scrolling message
// ---------------------------------------------------------------------------
const int MAX_CHARS = 60;

// Message timeline, ms from the start of the show.
const uint32_t FADE_OUT_MS = 450;     // heart rate fades to black
const uint32_t SCROLL_AT_MS = 650;    // the first letter starts out of the seam
const uint32_t BLACK_MS = 300;        // dark once the last letter has gone
const uint32_t FADE_IN_MS = 550;      // heart rate fades back in
const int REPEAT_GAP = 16;            // dark columns between one time round and the next

// The line's columns v = 0..63 start part way across block 3, the seam panel
// (ring column RING_AT_V0), so blocks 0-2 are the full-brightness middle and
// block 3 holds both ends. The text moves towards v = 0:
//   v  0..4    ring 59..63  letters fade out
//   v  5..52   ring 0..47   full brightness (blocks 0-2)
//   v 53..57   ring 48..52  letters fade in
//   v 58..63   ring 53..58  dark: the seam
const int SPAN = COLS;
const int FADE_COLS = 5;
const int RING_AT_V0 = COLS - FADE_COLS;
const int FULL_END = FADE_COLS + COLS - FACE;   // first v past the full-brightness part
const int DARK_FROM = FULL_END + FADE_COLS;

// How bright line column v is: fading out, full, fading in, dark.
inline float columnFade(int v) {
  if (v < 0) return 0;
  if (v < FADE_COLS) return (float)(v + 1) / (FADE_COLS + 1);
  if (v < FULL_END) return 1;
  if (v < DARK_FROM) return (float)(DARK_FROM - v) / (FADE_COLS + 1);
  return 0;
}

// Width of a character plus the gap after it.
inline int advance(char c) {
  const showfont::Glyph *g = showfont::find(PHRASE_FONT, c);
  return g ? showfont::width(g) + showfont::GAP : 0;
}

struct Message {
  char text[MAX_CHARS + 1];   // the phrase, uppercased, unknown characters dropped
  int len;
  int width;                  // in columns, gaps between letters included
  int repeats;                // times round
  float pxPerMs;
  uint32_t scrolledAt;        // the last letter has gone
  uint32_t fadeInAt, endAt;

  void build(const char *phrase, float pxPerS, int repeats_ = 1) {
    repeats = repeats_ < 1 ? 1 : repeats_;
    len = 0;
    for (int i = 0; phrase[i] && len < MAX_CHARS; i++) {
      char c = showfont::upper(phrase[i]);
      if (showfont::find(PHRASE_FONT, c) && !(c == ' ' && (len == 0 || text[len - 1] == ' '))) text[len++] = c;
    }
    while (len > 0 && text[len - 1] == ' ') len--;
    text[len] = 0;
    width = 0;
    for (int i = 0; i < len; i++) width += advance(text[i]);
    if (width > 0) width -= showfont::GAP;
    pxPerMs = (pxPerS < 1 ? 1 : pxPerS) / 1000.0f;
    scrolledAt = SCROLL_AT_MS + (uint32_t)(distance() / pxPerMs + 0.5f);
    fadeInAt = scrolledAt + BLACK_MS;
    endAt = fadeInAt + FADE_IN_MS;
  }

  // One copy and the gap after it.
  int period() const { return width + REPEAT_GAP; }
  // Every copy, the gaps between them included.
  int stripWidth() const { return repeats * width + (repeats - 1) * REPEAT_GAP; }
  // From the first column just inside the dark seam to the last one just past v = 0.
  float distance() const { return (float)(DARK_FROM + stripWidth()); }

  // The strip column shown at line column v = 0, in whole columns: -DARK_FROM
  // at the start (the text waits just inside the dark), stripWidth() at the end.
  int offset(uint32_t t) const {
    float o = t <= SCROLL_AT_MS ? 0 : (float)(t - SCROLL_AT_MS) * pxPerMs;
    if (o > distance()) o = distance();
    return (int)lroundf(o) - DARK_FROM;
  }
};

// Is strip column x, row y (0..PH-1) of the message lit? The plain per-pixel
// lookup; the renderer builds whole columns at once and is tested against it.
inline bool messageLit(const Message &m, int x, int y) {
  if (x < 0 || x >= m.stripWidth()) return false;
  x %= m.period();
  if (x >= m.width) return false;
  int x0 = 0;
  for (int i = 0; i < m.len; i++) {
    const showfont::Glyph *g = showfont::find(PHRASE_FONT, m.text[i]);
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
  Rgb heart[2];         // hot pink for you, purple for your friend
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
  int len;              // ticker columns between the two copies of the faces (0: none)
  int laps;
  float pxPerS;
  uint32_t cruiseMs, endAt;

  int put(int n, uint16_t bits, uint8_t kind, uint8_t person) {
    if (n < TICK_MAX) col[n] = TickCol{bits, kind, person};
    return n + 1;
  }
  int gap(int n, int w) { for (int i = 0; i < w; i++) n = put(n, 0, K_BLANK, 0); return n; }
  int bigText(int n, const char *s, uint8_t kind, uint8_t person) {
    for (int i = 0; s[i]; i++) {
      const showfont::Glyph *g = showfont::find(showfont::BOLD, s[i]);
      if (!g) continue;
      if (i) n = gap(n, showfont::GAP);
      for (int x = 0; x < showfont::width(g); x++) {
        uint16_t b = 0;
        for (int y = 0; y < showfont::BOLD.h; y++)
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

  // numbers: the heart / BPM / arrow ticker between the faces, or just the
  // faces going round. laps: full turns of the faces after the ticker.
  void build(const People &p, float pxPerS_, bool numbers = true, int laps_ = 1) {
    pxPerS = pxPerS_ < 5 ? 5 : pxPerS_;
    laps = laps_ < 1 ? 1 : laps_;
    int n = numbers ? gap(0, TICK_LEAD) : 0;
    int reps = numbers ? 2 : 0;
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
    float dist = distance();
    float rampPx = pxPerS * (SPIN_UP_MS + SPIN_DOWN_MS) / 2000.0f;
    float cruise = dist > rampPx ? (dist - rampPx) / pxPerS * 1000.0f : 0;
    cruiseMs = (uint32_t)cruise;
    endAt = SPIN_UP_MS + cruiseMs + SPIN_DOWN_MS + LOCK_MS;
  }

  float distance() const { return (float)(COLS * laps + len); }

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
  Message message;
  Ticker ticker;
  Rgb textRgb = {255, 255, 255};

  void startPhrase(const char *phrase, float pxPerS, Rgb text, int repeats = 1) {
    kind = PHRASE; textRgb = text;
    message.build(phrase, pxPerS, repeats);
  }
  void startTicker(const People &p, float pxPerS, bool numbers = true, int laps = 1) {
    kind = TICKER;
    ticker.build(p, pxPerS, numbers, laps);
  }
  // How far into the show a brightness scan needs to look. The ticker: all of
  // it. A message: once there's text all along the line, the picture repeats
  // every period(), and before and after that it's the same picture with
  // copies missing, so the first period plus a line's width covers every frame.
  uint32_t scanEnd() const {
    if (kind != PHRASE) return duration();
    const Message &m = message;
    int o = m.period() + SPAN;
    if (o > m.stripWidth()) o = m.stripWidth();
    uint32_t t = SCROLL_AT_MS + (uint32_t)((DARK_FROM + o) / m.pxPerMs) + 1;
    return t < m.endAt ? t : m.endAt;
  }
  uint32_t duration() const {
    return kind == PHRASE ? message.endAt : kind == TICKER ? ticker.endAt : 0;
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
    const Message &m = message;
    if (t < FADE_OUT_MS) {
      float k = 1.0f - (float)t / FADE_OUT_MS;
      for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) out[y][x] = scale(normal[y][x], k * k);
      return 0;
    }
    if (t >= m.fadeInAt) {
      float k = (float)(t - m.fadeInAt) / FADE_IN_MS;
      for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) out[y][x] = scale(normal[y][x], k * k);
      return 0;
    }
    // Line column v shows text column v + off. The lit rows of each line
    // column, built once per frame (looking each glyph up per pixel was too
    // slow on the board).
    const int off = m.offset(t);
    uint16_t bits[SPAN];
    for (int k = 0; k < SPAN; k++) bits[k] = 0;
    for (int rep = 0; rep < m.repeats; rep++) {
      int gx = rep * m.period();
      if (gx + m.width - off < 0 || gx - off >= SPAN) continue;   // this copy is off the line
      for (int i = 0; i < m.len && gx - off < SPAN; i++) {
        const showfont::Glyph *g = showfont::find(PHRASE_FONT, m.text[i]);
        int w = showfont::width(g);
        for (int cx = 0; cx < w; cx++) {
          int v = gx + cx - off;
          if (v < 0 || v >= SPAN) continue;
          for (int y = 0; y < PH; y++)
            if (showfont::lit(g, cx, y)) bits[v] |= (uint16_t)(1u << y);
        }
        gx += w + showfont::GAP;
      }
    }
    for (int v = 0; v < SPAN; v++) {
      float a = columnFade(v);
      if (!bits[v] || a <= 0.02f) continue;
      Rgb px = scale(textRgb, a);
      for (int y = 0; y < PH; y++)
        if ((bits[v] >> y) & 1) out[PHRASE_Y + y][(v + RING_AT_V0) % COLS] = px;
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
    // Whole columns only, as for the text: a sub-pixel blend streaks.
    const int oi = (int)lroundf(ticker.offset(t));
    for (int c = 0; c < COLS; c++)
      for (int y = 0; y < ROWS; y++)
        out[y][c] = stripAt(c + oi, y, normal, p);   // the bump can swing past the end: that wraps into the faces
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

// Festival mode: the spin and the phrases take turns, spin first, and each
// phrase turn plays the one after the last phrase played, round and round.
// last: the program played last (-1: none yet). lastPhrase: the last phrase
// played, 1..count-1 (0: none yet). Without the spin (tickerOk false) it's
// just the phrases in turn.
inline int nextAlternating(int last, int lastPhrase, int count, bool tickerOk) {
  const int phrases = count - 1;
  if (tickerOk && (last != 0 || phrases <= 0)) return 0;
  if (phrases <= 0) return -1;
  int p = lastPhrase >= 1 && lastPhrase <= phrases ? lastPhrase : 0;   // 0..phrases-1 next
  return 1 + p % phrases;
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
