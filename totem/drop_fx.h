#pragma once

// ============================================================================
// BEAT DROP: WHAT THE RING SHOWS (no hardware here, so the host tests cover it)
// ============================================================================
//
// burst(): the drop itself, about 2.6 s, over whatever was on the ring.
//   0-90 ms     the whole ring flashes white, once (no strobing)
//   90-500 ms   two rainbow shock bands race from the middle to the top and
//               bottom edges while the white dies away
//   300-2600    rainbow stripes spin round the ring and fade, with sparkles;
//               the heart rate (or the show) fades back in underneath
//
// meter(): DROP_TEST_MODE's sound meter, the same on all four faces, so it
// reads from any side. Top to bottom:
//   row 0       loudness at the mic (green, amber near the top, red pixel = clipping)
//   row 1       mic gain, one blue pixel per 3 dB
//   rows 2-13   the bass over the last 8 s, one column per half second, newest
//               on the right. The yellow line is the baseline (the room's usual
//               bass), the red line QUIET_DB under it: bars under the red line are
//               "bass out". Cyan = listening, purple = bass out (a breakdown),
//               a white column = a drop fired there.
//   rows 14-15  state: orange filling = still learning the room; green =
//               listening; purple filling = bass out, full and blinking once
//               it has been out long enough to count; amber pixels from the
//               right = cooldown after a drop; dim red = too quiet to judge.

#include <stdint.h>
#include <math.h>
#include "drop_detect.h"

namespace dropfx {

const int COLS = 64, ROWS = 16, FW = 16;
const uint32_t BURST_MS = 2600;

struct Px { uint8_t r, g, b; };
typedef Px Frame[ROWS][COLS];

inline Px hsv(uint8_t h, uint8_t v) {   // full saturation, h 0-255 round the wheel
  uint8_t region = h / 43, rem = (uint8_t)((h - region * 43) * 6);
  uint8_t q = (uint8_t)(v * (255 - rem) / 255), t = (uint8_t)(v * rem / 255);
  switch (region) {
    case 0:  return {v, t, 0};
    case 1:  return {q, v, 0};
    case 2:  return {0, v, t};
    case 3:  return {0, q, v};
    case 4:  return {t, 0, v};
    default: return {v, 0, q};
  }
}

inline Px scale(Px p, float k) {
  if (k <= 0) return {0, 0, 0};
  if (k >= 1) return p;
  return {(uint8_t)(p.r * k), (uint8_t)(p.g * k), (uint8_t)(p.b * k)};
}

inline Px lighten(Px a, Px b) { return {a.r > b.r ? a.r : b.r, a.g > b.g ? a.g : b.g, a.b > b.b ? a.b : b.b}; }

inline uint32_t hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }

// Draws the burst t ms after the drop over the frame already in f. False (and
// f untouched) once it's over.
inline bool burst(uint32_t t, Frame &f) {
  if (t >= BURST_MS) return false;
  const float ft = (float)t;
  // How much of what was there shows through: none until 600 ms, all by the end.
  float keep = ft < 600 ? 0 : (ft - 600) / (BURST_MS - 600);
  keep = keep * keep;
  float white = ft < 90 ? 1.0f : ft < 240 ? 1.0f - (ft - 90) / 150 : 0;
  // Shock bands: distance from the middle (7.5 rows) grows to 9 by 500 ms.
  float sb = ft >= 60 && ft < 560 ? (ft - 60) / 500 * 9.0f : -10;
  float sbFade = ft < 560 ? 1.0f - (ft - 60) / 700 : 0;
  // Spinning stripes from 300 ms, fading out by the end.
  float sp = ft < 300 ? 0 : ft < 500 ? (ft - 300) / 200 : 1.0f - (ft - 500) / (BURST_MS - 500);
  if (sp < 0) sp = 0;
  sp = sp * sp * (ft < 500 ? 1 : 1);
  float spin = ft * 0.045f;   // columns moved: ~45 columns a second
  for (int y = 0; y < ROWS; y++) {
    float dy = fabsf(y - 7.5f);
    for (int x = 0; x < COLS; x++) {
      Px out = scale(f[y][x], keep);
      if (white > 0) out = lighten(out, scale(Px{255, 255, 255}, white));
      if (sb > -1) {
        float d = fabsf(dy - sb);
        if (d < 1.5f) {
          float k = (1.0f - d / 1.5f) * sbFade;
          out = lighten(out, hsv((uint8_t)(x * 4 + t / 6), (uint8_t)(255 * k)));
        }
      }
      if (sp > 0) {
        // Diagonal stripes, 8 columns apart, leaning one row per column.
        float u = fmodf(x + y * 0.75f + spin, 8.0f);
        if (u < 0) u += 8;
        float k = u < 3 ? 1.0f - fabsf(u - 1.5f) / 1.5f : 0;
        if (k > 0) out = lighten(out, hsv((uint8_t)(x * 4 - t / 8), (uint8_t)(230 * k * sp)));
        // Sparkles: a few random pixels, each lasting 50 ms.
        uint32_t h = hash((uint32_t)(x * 131 + y * 7919) ^ (t / 50) * 2654435761u);
        if ((h & 0xFF) < 6) out = lighten(out, scale(Px{255, 255, 255}, sp));
      }
      f[y][x] = out;
    }
  }
  return true;
}

// The meter's last 8 s: one column every half second.
struct Meter {
  static const int N = FW;
  static const uint32_t COL_MS = 500;
  float rel[N];          // bass minus baseline, dB (the lowest in that half second)
  uint8_t kind[N];       // 0 warm-up, 1 listening, 2 bass out, 3 drop
  int head = 0;
  uint32_t lastPush = 0, seenDrops = 0;
  bool started = false;
  float curMin = 99;
  uint8_t curKind = 0;
  Meter() { for (int i = 0; i < N; i++) { rel[i] = -99; kind[i] = 0; } }

  void push(const drop::Snapshot &s) {
    float r = s.bassDb - s.baseDb;
    if (r < curMin) curMin = r;
    uint8_t k = s.phase == drop::WARMUP ? 0 : (s.phase == drop::QUIET || s.candidate) ? 2 : 1;
    if (s.drops != seenDrops) { seenDrops = s.drops; k = 3; }
    if (k > curKind) curKind = k;
    if (!started) { started = true; lastPush = s.ms; }
    if (s.ms - lastPush < COL_MS) return;
    lastPush = s.ms;
    head = (head + 1) % N;
    rel[head] = curMin; kind[head] = curKind;
    curMin = 99; curKind = 0;
  }
};

// Row for a level relative to the baseline: +6 dB at row 2, 3 dB a row.
inline int meterRow(float rel) {
  int r = 2 + (int)floorf((6.0f - rel) / 3.0f);
  return r < 2 ? 2 : r > 13 ? 13 : r;
}

// Draws the test-mode meter over the whole ring. ok = false: the mic didn't
// start (all red, see the serial log).
inline void meter(const Meter &m, const drop::Snapshot &s, float quietDb, float gainDb, uint32_t ms, bool ok,
                  Frame &f) {
  Px face[ROWS][FW];
  for (int y = 0; y < ROWS; y++) for (int x = 0; x < FW; x++) face[y][x] = {0, 0, 0};
  if (!ok) {
    for (int y = 0; y < ROWS; y++) for (int x = 0; x < FW; x++) if (x == y || x == FW - 1 - y) face[y][x] = {120, 0, 0};
  } else {
    // Row 0: loudness at the mic, -48..0 dBFS.
    int n = (int)((s.peakDb + 48.0f) / 3.0f + 0.5f);
    if (n < 0) n = 0;
    if (n > FW) n = FW;
    for (int x = 0; x < n; x++) face[0][x] = x >= 13 ? Px{200, 120, 0} : Px{0, 120, 0};
    if (s.clipping) face[0][FW - 1] = {255, 0, 0};
    // Row 1: gain.
    int g = (int)(gainDb / 3.0f + 0.5f);
    for (int x = 0; x < g && x < FW; x++) face[1][x] = {0, 0, 120};
    // Rows 2-13: history. Baseline and threshold lines first, bars over them.
    int baseRow = meterRow(0), thrRow = meterRow(-quietDb);
    for (int x = 0; x < FW; x++) { face[baseRow][x] = {60, 50, 0}; face[thrRow][x] = {60, 0, 0}; }
    for (int c = 0; c < FW; c++) {
      int i = (m.head + 1 + c) % Meter::N;
      if (m.rel[i] < -90) continue;
      if (m.kind[i] == 3) { for (int y = 2; y <= 13; y++) face[y][c] = {255, 255, 255}; continue; }
      Px col = m.kind[i] == 0 ? Px{70, 70, 70} : m.kind[i] == 2 ? Px{160, 0, 220} : Px{0, 170, 200};
      for (int y = meterRow(m.rel[i]); y <= 13; y++) face[y][c] = col;
    }
    // Rows 14-15: state.
    Px st[FW];
    for (int x = 0; x < FW; x++) st[x] = {0, 0, 0};
    if (s.tooQuiet && s.phase != drop::WARMUP) {
      for (int x = 0; x < FW; x++) st[x] = {50, 0, 0};
    } else if (s.phase == drop::WARMUP) {
      int k = (int)(s.warmupFrac * FW);
      for (int x = 0; x < k && x < FW; x++) st[x] = {200, 70, 0};
    } else if (s.phase == drop::QUIET) {
      bool on = (ms / 250) % 2;
      for (int x = 0; x < FW; x++) st[x] = on ? Px{180, 0, 255} : Px{70, 0, 100};
    } else if (s.candidate) {
      int k = (int)(s.quietS / 5.0f * FW);
      for (int x = 0; x < k && x < FW; x++) st[x] = {150, 0, 210};
    } else {
      for (int x = 0; x < FW; x++) st[x] = {0, 90, 0};
    }
    if (s.cooldownS > 0) {
      int k = (int)(s.cooldownS / 90.0f * FW + 0.99f);
      for (int x = FW - k; x < FW; x++) if (x >= 0) st[x] = {200, 120, 0};
    }
    for (int x = 0; x < FW; x++) { face[14][x] = st[x]; face[15][x] = st[x]; }
  }
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++) f[y][x] = face[y][x % FW];
}

} // namespace dropfx
