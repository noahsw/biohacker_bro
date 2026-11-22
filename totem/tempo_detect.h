#pragma once

// ============================================================================
// MUSIC TEMPO (BPM) — no hardware here, so the host tests can feed it audio
// ============================================================================
//
// What it listens for: the kick drum. In house, trance and techno the kick
// lands on every beat, so the moments the low end suddenly gets louder (the
// "onsets") come at the song's tempo. This works out those onsets from the
// kick band (about 40-200 Hz) every 10 ms, keeps the last 8 seconds of them,
// and asks which spacing between them repeats best (autocorrelation: line the
// 8 seconds up against itself, shifted, and see which shift matches).
//
// The answer always comes out in SHOWN_MIN..SHOWN_MAX (88-176 by default, a
// factor of two wide), doubling or halving as needed. A half-time dubstep
// groove whose kicks come every other beat (70 "BPM" of kicks) shows as 140,
// and a 70 BPM downtempo track shows as 140 too. That's the usual DJ habit,
// and it means the number never flips between 64 and 128 on the same song.
//
// Then it holds the number steady:
//   - it only "locks" once LOCK_S of good estimates agree within AGREE_BPM,
//   - a locked number follows slow drift, and only jumps to a new tempo after
//     SWITCH_S of estimates agreeing on it (the next track),
//   - through a breakdown with no kick the estimates go bad, and the lock
//     holds on, beats and all, for up to HOLD_S, since the tempo doesn't
//     change in a breakdown. Longer than that (or silence) and it unlocks.
//
// It also keeps a beat clock: once locked, it predicts every beat and nudges
// the prediction toward where the kicks actually land, so a display can pulse
// on the beat rather than at a guessed phase.
//
// Everything runs on a clock made of the audio itself (HOP_MS steps), so a
// test can push minutes of synthetic music through it in a second.

#include "drop_detect.h"   // drop::Biquad

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace tempo {

struct Params {
  float shownMin   = 88.0f;    // the number shown is always in [shownMin, 2 x shownMin)
  float confMin    = 0.30f;    // an estimate this sure (0..1) or more counts as good
  float lockS      = 2.0f;     // good estimates agreeing this long before it locks
  float agreeBpm   = 1.5f;     // "agreeing": all within this of their median
  float switchS    = 4.0f;     // a different tempo this long replaces the locked one
  float holdS      = 30.0f;    // no good estimate for this long: unlock (a breakdown keeps it)
  float minLevelDb = -80.0f;   // kick band quieter than this (dBFS, gain taken off): silence
  float onsetMinDb = 1.5f;     // a rise smaller than this isn't a hit (held notes ripple by less)
  float preferBpm  = 126.0f;   // when two answers are an octave apart, lean toward this
  float phaseGain  = 0.3f;     // how hard each look at the kicks pulls the beat clock
};

struct Snapshot {
  bool locked = false;
  float bpm = 0;            // the locked tempo, smoothed (0 when not locked)
  int shown = 0;            // whole number to show (0 = nothing to show)
  float rawBpm = 0;         // the latest estimate, locked or not (0 = none yet)
  float conf = 0;           // how sure that estimate is, 0..1
  float levelDb = -120;     // the kick band's level over the last 8 s, gain taken off
  float sinceGoodS = 0;     // seconds since the last good estimate
  float periodMs = 0;       // one beat, at the locked tempo
  uint32_t lastBeatMs = 0;  // audio clock: the last predicted beat
  uint32_t beats = 0;       // predicted beats since it locked (for counting 1-2-3-4)
  uint32_t ms = 0;          // audio clock now
  uint32_t locks = 0;       // times it has locked (or switched tempo)
};

class Tracker {
public:
  static const int FS = 16000;
  static const int DECIM = 8;               // the kick band is worked out at 2 kHz
  static const int HOP = 20;                // 20 samples at 2 kHz = 10 ms
  static const int HOP_MS = 10;
  static const int N = 800;                 // 8 s of onsets
  static const int UPDATE_HOPS = 25;        // a fresh estimate 4 times a second
  static const int MIN_HOPS = 400;          // 4 s of sound before the first estimate
  static const int LAG_MIN = 33;            // 180 BPM
  static const int LAG_MAX = 136;           // 44 BPM (a half-time 88 groove: kicks every other beat)
  static const int R_MIN = 15;              // lags worked out from here (half of LAG_MIN, and a bit)
  static const int HIST = 32;               // recent good estimates kept (8 s)

  void begin(const Params &p) {
    P = p;
    aa1.set(FS, 400, false); aa2.set(FS, 400, false);
    const float fs2 = (float)FS / DECIM;
    hp.set(fs2, 40, true); lp.set(fs2, 200, false);
    reset();
  }

  // Interleaved stereo 16-bit (the two mics). gainDb: the mic gain the
  // samples were recorded at, taken off every level.
  void feed(const int16_t *lr, int frames, float gainDb) {
    gain = gainDb;
    for (int i = 0; i < frames; i++) {
      float x = (lr[2 * i] + lr[2 * i + 1]) * (0.5f / 32768.0f);
      float d = aa2.run(aa1.run(x));
      if (++decimPhase < DECIM) continue;
      decimPhase = 0;
      float b = lp.run(hp.run(d));
      hopAcc += b * b;
      if (++hopN >= HOP) endHop();
    }
  }

  // Call when the mic gain changes: the next hops are skipped (no onsets)
  // while the codec settles, so the jump in level isn't heard as a kick.
  void noteGainChange() { skip = 10; }

  const Snapshot &snapshot() const { return S; }

  // Forget everything (a new set, the totem moved).
  void reset() {
    memset(env, 0, sizeof(env)); memset(pw, 0, sizeof(pw)); memset(eHist, 0, sizeof(eHist)); memset(aHist, 0, sizeof(aHist));
    envI = 0; hops = 0; sinceUpdate = 0; pwI = 0;
    locked = false; bpm = 0; shown = 0; histN = 0; offN = 0; sinceGood = 0;
    lastBeat = nextBeat = 0; beatsN = 0;
    Snapshot fresh;
    fresh.locks = S.locks;
    S = fresh;
  }

private:
  Params P;
  drop::Biquad aa1, aa2, hp, lp;
  int decimPhase = 0, hopN = 0, skip = 0;
  float hopAcc = 0, gain = 0;

  float env[N];          // onset strength per hop, ring buffer
  float pw[N];           // kick band power per hop (gain off), for the level
  int envI = 0, pwI = 0;
  uint32_t hops = 0;     // hops so far (the audio clock)
  int sinceUpdate = 0;
  float eHist[2], aHist[2];   // the last two hops' level (dB) and amplitude, for the onset
  float pHist[3] = {0, 0, 0};

  float work[N];         // the 8 s, oldest first, mean taken off
  float raw[2 * LAG_MAX + 4] = {0}, r[2 * LAG_MAX + 4] = {0};

  bool locked = false;
  float bpm = 0;
  int shown = 0;
  float hist[HIST];      // recent good estimates
  int histN = 0;
  float offs[HIST];      // recent good estimates that disagree with the lock
  int offN = 0;
  float sinceGood = 0;
  double lastBeat = 0, nextBeat = 0;   // in hops
  uint32_t beatsN = 0;
  Snapshot S;

  static float db(double p) { return 10.0f * log10f((float)p + 1e-15f); }

  // The onset strength: how much the kick band (over the last 30 ms, so a
  // 55 Hz bass note doesn't ripple it) got louder in the last 20 ms. Only
  // rises count, and only rises of onsetMinDb or more (a held note wobbles
  // by less). The size of the rise is in amplitude, not dB: a kick that's
  // twice as loud as the off-beat bass then counts for twice as much, which
  // is what tells the beat (kick to kick) from one and a half beats (kick to
  // bass note). In dB they'd look the same. Under the silence floor nothing
  // counts, so hiss makes no onsets.
  void endHop() {
    float p = hopAcc / HOP * powf(10.0f, -gain / 10.0f);
    hopAcc = 0; hopN = 0;
    hops++;
    pHist[2] = pHist[1]; pHist[1] = pHist[0]; pHist[0] = p;
    float pm = (pHist[0] + pHist[1] + pHist[2]) / 3.0f;
    float e = db(pm), a = sqrtf(pm);
    float floorDb = P.minLevelDb - 10.0f;
    if (e < floorDb) { e = floorDb; a = 0; }
    float on = e - eHist[1] >= P.onsetMinDb ? a - aHist[1] : 0;
    if (on < 0) on = 0;
    eHist[1] = eHist[0]; eHist[0] = e;
    aHist[1] = aHist[0]; aHist[0] = a;
    if (skip > 0) { skip--; on = 0; eHist[1] = eHist[0]; aHist[1] = aHist[0]; }
    env[envI] = on; envI = (envI + 1) % N;
    pw[pwI] = p; pwI = (pwI + 1) % N;
    stepBeatClock();
    if (++sinceUpdate >= UPDATE_HOPS) { sinceUpdate = 0; update(); }
    S.ms = hops * HOP_MS;
  }

  void stepBeatClock() {
    if (!locked) return;
    while ((double)hops >= nextBeat) {
      lastBeat = nextBeat;
      nextBeat += 6000.0 / bpm;
      beatsN++;
    }
    S.lastBeatMs = (uint32_t)(lastBeat * HOP_MS);
    S.beats = beatsN;
  }

  // env at absolute hop h (h < hops), 0 if it's older than the buffer.
  float envAt(long h) const {
    long age = (long)hops - 1 - h;
    if (age < 0 || age >= N) return 0;
    return env[(envI - 1 - age + 2 * N) % N];
  }

  static float parabolic(const float *v, int i) {
    float a = v[i - 1], b = v[i], c = v[i + 1], den = a - 2 * b + c;
    if (den >= 0) return (float)i;
    float o = 0.5f * (a - c) / den;
    return i + (o > 0.5f ? 0.5f : o < -0.5f ? -0.5f : o);
  }

  // The hop where the kicks land, measured from the last 8 s at period per
  // (in hops): the offset back from now that the most onset lines up with.
  double measureLastBeat(double per) const {
    int pi = (int)ceil(per);
    float best = -1; int bo = 0;
    for (int o = 0; o < pi; o++) {
      float s = 0;
      for (int k = 0; k < 8; k++) {
        long h = (long)hops - 1 - o - (long)lround(k * per);
        s += envAt(h);
      }
      if (s > best) { best = s; bo = o; }
    }
    // The onset peaks about a hop after the kick hits (the 20 ms difference).
    return (double)hops - 1 - bo - 1;
  }

  void estimate(float &outBpm, float &outConf, float &outLevel) {
    // The kick band's level over the window.
    double ps = 0;
    for (int i = 0; i < N; i++) ps += pw[i];
    outLevel = db(ps / N);
    outBpm = 0; outConf = 0;
    if (outLevel < P.minLevelDb) return;

    double mean = 0;
    for (int i = 0; i < N; i++) { work[i] = env[(envI + i) % N]; mean += work[i]; }
    mean /= N;
    double r0 = 0;
    for (int i = 0; i < N; i++) { work[i] -= (float)mean; r0 += work[i] * work[i]; }
    if (r0 < 1e-9) return;
    const int LMAX2 = 2 * LAG_MAX + 3;
    for (int L = R_MIN; L <= LMAX2; L++) {
      double s = 0;
      for (int n = L; n < N; n++) s += work[n] * work[n - L];
      raw[L] = (float)(s / r0 * N / (N - L));
    }
    // Onsets land on 10 ms steps, so a beat of 46.5 steps (129 BPM) splits
    // its peak between 46 and 47. Smoothing across neighbouring lags puts it
    // back together.
    for (int L = R_MIN + 1; L < LMAX2; L++) r[L] = 0.25f * raw[L - 1] + 0.5f * raw[L] + 0.25f * raw[L + 1];
    r[R_MIN] = raw[R_MIN]; r[LMAX2] = raw[LMAX2];
    // Score each beat length by how well it repeats, how well twice it
    // repeats (a real beat repeats at both; a fluke usually doesn't), and how
    // well half of it does (the off-beat hats and bass sit half a beat after
    // the kick: that's what tells the beat from one and a half beats, which
    // also lands kick on bass note). A mild lean toward preferBpm picks
    // between octaves.
    float score[LAG_MAX + 2];
    float best = -1e9f; int bl = LAG_MIN;
    for (int L = LAG_MIN; L <= LAG_MAX + 1; L++) {
      float half = 0.5f * (r[L / 2] + r[(L + 1) / 2]);
      score[L] = (r[L] + 0.5f * r[2 * L] + 0.25f * half) / 1.75f;
    }
    for (int L = LAG_MIN + 1; L <= LAG_MAX; L++) {
      float b = 6000.0f / L;
      float lo = log2f(b / P.preferBpm);
      float w = expf(-0.5f * lo * lo);
      float v = score[L] * w;
      if (v > best) { best = v; bl = L; }
    }
    // Median score: how much the winner stands out from the rest.
    float sorted[LAG_MAX + 2];
    int m = 0;
    for (int L = LAG_MIN; L <= LAG_MAX; L++) sorted[m++] = score[L];
    for (int i = 1; i < m; i++) { float x = sorted[i]; int j = i - 1; while (j >= 0 && sorted[j] > x) { sorted[j + 1] = sorted[j]; j--; } sorted[j + 1] = x; }
    float med = sorted[m / 2];
    float c = score[bl] - med;
    outConf = c < 0 ? 0 : c > 1 ? 1 : c;

    // Sharpen the beat length: in-between peak at it and at twice it.
    float l1 = parabolic(score, bl);
    int b2 = 2 * bl;
    for (int L = 2 * bl - 2; L <= 2 * bl + 2; L++) if (L > LAG_MIN && L < LMAX2 && r[L] > r[b2]) b2 = L;
    float lref = l1;
    if (b2 > LAG_MIN && b2 < LMAX2) lref = (l1 + parabolic(r, b2) / 2) / 2;
    float b = 6000.0f / lref;
    while (b < P.shownMin) b *= 2;
    while (b >= 2 * P.shownMin) b /= 2;
    outBpm = b;
  }

  static float median(const float *v, int n) {
    float t[HIST];
    for (int i = 0; i < n; i++) t[i] = v[i];
    for (int i = 1; i < n; i++) { float x = t[i]; int j = i - 1; while (j >= 0 && t[j] > x) { t[j + 1] = t[j]; j--; } t[j + 1] = x; }
    return n ? t[n / 2] : 0;
  }

  // The last k entries all within agree of their median?
  bool agree(const float *v, int n, int k, float &med) const {
    if (n < k) return false;
    med = median(v + n - k, k);
    for (int i = n - k; i < n; i++) if (fabsf(v[i] - med) > P.agreeBpm) return false;
    return true;
  }

  static void push(float *v, int &n, float x) {
    if (n == HIST) { memmove(v, v + 1, (HIST - 1) * sizeof(float)); n--; }
    v[n++] = x;
  }

  void lockTo(float b) {
    bpm = b;
    shown = (int)lroundf(b);
    double per = 6000.0 / b;
    lastBeat = measureLastBeat(per);
    nextBeat = lastBeat + per;
    if (!locked) beatsN = 0;
    locked = true;
    offN = 0;
    S.locks++;
  }

  void update() {
    if (hops < (uint32_t)MIN_HOPS) return;
    const float dt = UPDATE_HOPS * HOP_MS / 1000.0f;
    float raw, conf, level;
    estimate(raw, conf, level);
    S.rawBpm = raw; S.conf = conf; S.levelDb = level;
    bool good = raw > 0 && conf >= P.confMin;
    const int kLock = (int)(P.lockS / dt + 0.5f), kSwitch = (int)(P.switchS / dt + 0.5f);

    if (!good) {
      sinceGood += dt;
      if (!locked) histN = 0;   // locking needs a run of good ones
      if (locked && (sinceGood > P.holdS || level < P.minLevelDb)) { locked = false; bpm = 0; shown = 0; histN = 0; offN = 0; }
    } else {
      sinceGood = 0;
      push(hist, histN, raw);
      float med;
      if (!locked) {
        if (agree(hist, histN, kLock, med)) lockTo(med);
      } else if (fabsf(raw - bpm) <= 2 * P.agreeBpm) {
        bpm += 0.15f * (raw - bpm);
        offN = 0;
        // Pull the beat clock toward where the kicks land.
        double per = 6000.0 / bpm;
        double err = measureLastBeat(per) - lastBeat;
        err -= per * floor(err / per + 0.5);   // nearest beat, -per/2..per/2
        lastBeat += P.phaseGain * err;
        nextBeat = lastBeat + per;
        while (nextBeat <= (double)hops - per) nextBeat += per;
      } else {
        push(offs, offN, raw);
        if (agree(offs, offN, kSwitch, med)) lockTo(med);
      }
      if (locked && fabsf(bpm - shown) > 0.7f) shown = (int)lroundf(bpm);
    }

    S.locked = locked;
    S.bpm = locked ? bpm : 0;
    S.shown = locked ? shown : 0;
    S.sinceGoodS = sinceGood;
    S.periodMs = locked ? 60000.0f / bpm : 0;
  }
};

} // namespace tempo
