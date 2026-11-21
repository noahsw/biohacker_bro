#pragma once

// ============================================================================
// BEAT DROP DETECTOR — no hardware here, so the host tests can feed it audio
// ============================================================================
//
// A trance or house drop, in sound: the kick and bass have been gone for a
// while (the breakdown, then the build with its snare rolls and risers), then
// they slam back in all at once. So this listens to the bass band only
// (about 40-150 Hz, where the kick and the bass line live) and looks for
// exactly that shape:
//
//   1. Bass well under its usual level (QUIET_DB under the baseline) for at
//      least BUILD_MIN_S, while the rest of the music keeps going (that's
//      what tells a breakdown from silence between sets). The bass has to
//      have been in first: music fading up out of silence with the beat
//      still to come isn't a breakdown, so its first kick isn't a drop.
//   2. Then, within RISE_MS of leaving the quiet, bass back to near its usual
//      level and JUMP_DB above where the breakdown sat, held for a few
//      blocks so a single thump doesn't count.
//
// Or the other shape house uses: the build keeps (most of) its bass, a short
// gap where the bass cuts out (GAP_DB under, even half a second), then it
// comes back louder than the groove ever was: SURGE_DB over the usual level,
// averaged over a whole second, starting within SURGE_WIN_MS of the gap. The
// bass has to have been in before the gap, as for a breakdown.
//
// "Usual level" calibrates itself: once a second it notes the bass level,
// keeps the last minute of those, and takes the 75th percentile, so it
// follows the room (a living room, a festival stage) without any setting.
// It stops learning during a breakdown, so a minute-long trance breakdown
// doesn't become the new normal. Levels are worked out net of the mic gain,
// so a gain change (see gainAdvice()) doesn't look like a drop.
//
// Everything runs on a clock made of the audio itself (blocks of BLOCK_MS),
// so a test can push ten minutes of synthetic music through it in a second.

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace drop {

struct Params {
  float quietDb    = 9.0f;    // bass this far under the baseline = bass is out
  float buildMinS  = 2.0f;    // bass out at least this long before a drop counts
  float buildMaxS  = 120.0f;  // out longer than this: not a breakdown; relearn the room
  float returnDb   = 3.0f;    // drop: bass back to within this of the baseline...
  float jumpDb     = 10.0f;   // ...and at least this far above the breakdown's level...
  float riseMs     = 700.0f;  // ...within this long of the bass last being out
  float riseDb     = 8.0f;    // ...and at least this far up on where it was riseMs (+300 ms) ago: a slam, not a sweep
  float gapDb      = 12.0f;   // a gap: bass this far under the baseline (any length)...
  float surgeDb    = 5.0f;    // ...then a whole second of bass this far OVER it...
  float surgeWinMs = 3000.0f; // ...ending within this long of the gap
  float cooldownS  = 30.0f;   // no second drop for this long
  float silenceDb  = 20.0f;   // whole sound this far under its baseline = silence, not a breakdown
  float minLevelDb = -80.0f;  // bass baseline must be above this (dBFS at 0 dB gain): ignore a quiet room
  float graceMs    = 600.0f;  // bass back for less than this doesn't end the quiet (a stray thump)
  float warmupS    = 20.0f;   // seconds of sound before it trusts the baseline
};

// One biquad (RBJ cookbook), transposed direct form II, float.
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void set(float fs, float f, bool high) {
    const float q = 0.7071f;
    float w = 2.0f * 3.14159265f * f / fs, c = cosf(w), al = sinf(w) / (2.0f * q), a0 = 1.0f + al;
    if (high) { b0 = (1 + c) / 2; b1 = -(1 + c); } else { b0 = (1 - c) / 2; b1 = 1 - c; }
    b2 = b0;
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 = -2 * c / a0; a2 = (1 - al) / a0;
    z1 = z2 = 0;
  }
  float run(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

enum Phase : uint8_t { WARMUP, LISTEN, QUIET };

// What the meter and the serial log show.
struct Snapshot {
  Phase phase = WARMUP;
  bool candidate = false;   // bass out, not yet for BUILD_MIN_S
  float bassDb = -120;      // bass now (300 ms window), dBFS at 0 dB gain
  float baseDb = -120;      // its baseline
  float fullDb = -120, fullBaseDb = -120;
  float quietS = 0;         // how long the bass has been out
  float warmupFrac = 0;     // 0..1 during WARMUP
  float cooldownS = 0;      // seconds left before another drop can fire
  float peakDb = -120;      // loudest sample in the last second, raw dBFS (gain not taken off)
  bool clipping = false;    // samples hit full scale in the last second
  bool tooQuiet = false;    // baseline under minLevelDb: won't fire
  float lastJumpDb = 0, lastBuildS = 0;   // the last drop: how far the bass jumped, after how long out
  bool lastSurge = false;   // the last drop was a gap and surge, not a breakdown
  uint32_t drops = 0;       // drops so far
  uint32_t blocked = 0;     // drops the cooldown swallowed
  uint32_t ms = 0;          // audio clock
};

class Detector {
public:
  static const int FS = 16000;
  static const int DECIM = 8;                  // the bass band is worked out at 2 kHz
  static const int BLOCK = 320;                // 20 ms
  static const int BLOCK_MS = 20;
  static const int WIN = 15;                   // 300 ms: smooths over the gaps between kicks
  static const int SECS = 60;                  // a minute of once-a-second levels for the baseline
  static const int HOLD_BLOCKS = 10;           // drop condition held this many blocks (200 ms): a lone hit in the breakdown isn't a drop
  static const int RISE_HIST = 64;             // 1.28 s of bass levels, for "did it jump just now"

  void begin(const Params &p) {
    P = p;
    aa1.set(FS, 400, false); aa2.set(FS, 400, false);
    const float fs2 = (float)FS / DECIM;
    hp.set(fs2, 40, true); lp1.set(fs2, 150, false); lp2.set(fs2, 150, false);
    dcHp.set(FS, 30, true);
    reset();
  }

  // Interleaved stereo 16-bit (the two mics). gainDb: the mic gain the
  // samples were recorded at, taken off every level.
  void feed(const int16_t *lr, int frames, float gainDb) {
    gain = gainDb;
    for (int i = 0; i < frames; i++) {
      int16_t l = lr[2 * i], r = lr[2 * i + 1];
      int al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
      int a = al > ar ? al : ar;
      if (a > secPeak) secPeak = a;
      if (a >= 32000) secClip++;
      float x = (l + r) * (0.5f / 32768.0f);
      float f = dcHp.run(x);
      blkFull += f * f;
      float d = aa2.run(aa1.run(x));
      if (++decimPhase >= DECIM) {
        decimPhase = 0;
        float b = lp2.run(lp1.run(hp.run(d)));
        blkBass += b * b;
      }
      if (++blkN >= BLOCK) endBlock();
    }
  }

  // Call when the mic gain changes: the next blocks are skipped while the
  // codec settles.
  void noteGainChange() { skip = 8; }

  // Once per second at most: -1 to turn the mic gain down a step (it clipped),
  // +1 to turn it up (very quiet for 20 s), 0 to leave it. Never during a
  // breakdown. Consumed when read.
  int gainAdvice() { int a = advice; advice = 0; return a; }

  const Snapshot &snapshot() const { return S; }

  // Forget the room (a new set, the totem moved).
  void reset() {
    memset(win, 0, sizeof(win)); memset(fwin, 0, sizeof(fwin));
    winN = winI = 0; nSecs = secI = 0;
    secBass = secFull = 0; secBlocks = 0;
    phase = WARMUP; candidate = false; armed = false; quietStart = 0; aboveSince = 0; lastQuiet = 0;
    bdSum = 0; bdN = 0; holdRun = 0; histI = histN = 0; gapAt = 0;
    S.phase = WARMUP; S.candidate = false;
  }

private:
  Params P;
  Biquad aa1, aa2, hp, lp1, lp2, dcHp;
  int decimPhase = 0, blkN = 0, skip = 0;
  float blkBass = 0, blkFull = 0, gain = 0;
  int secPeak = 0, secClip = 0, quietPeakS = 0, secTick = 0;
  int advice = 0;

  float win[WIN], fwin[WIN];      // last 300 ms of block powers, gain taken off
  int winN = 0, winI = 0;
  float secs[SECS], fsecs[SECS];  // last minute of 1-second levels (dB)
  int nSecs = 0, secI = 0;
  double secBass = 0, secFull = 0;
  int secBlocks = 0;
  float base = -120, fullBase = -120;

  uint32_t now = 0, cooldownUntil = 0;
  Phase phase = WARMUP;
  bool candidate = false;
  bool armed = false;       // bass has been in since the last silence: a breakdown can start
  uint32_t quietStart = 0, aboveSince = 0, lastQuiet = 0;
  double bdSum = 0;
  int bdN = 0, holdRun = 0;
  float hist[RISE_HIST];          // recent bass levels (dB), one per block
  int histI = 0, histN = 0;
  uint32_t gapAt = 0;             // last block of a gap (bass GAP_DB under), 0 = none
  float gapL = 0;                 // how low the bass went in it
  Snapshot S;

  // The mean bass level (dB) over the last ms milliseconds.
  float recentMean(float ms) const {
    int n = (int)(ms / BLOCK_MS);
    if (n > histN) n = histN;
    if (n <= 0) return -120;
    float sum = 0;
    for (int i = 0; i < n; i++) sum += hist[(histI - 1 - i + RISE_HIST) % RISE_HIST];
    return sum / n;
  }

  // A drop counts unless the cooldown is on. Either way, back to listening.
  void fire(float jumpDb, float buildS, bool surge) {
    if (now >= cooldownUntil) {
      S.drops++;
      S.lastJumpDb = jumpDb;
      S.lastBuildS = buildS;
      S.lastSurge = surge;
      cooldownUntil = now + (uint32_t)(P.cooldownS * 1000);
    } else {
      S.blocked++;
    }
    phase = LISTEN; candidate = false; holdRun = 0; gapAt = 0;
  }

  // The lowest bass level in the last ms milliseconds (the window's own
  // 300 ms included, so a slam that the window smears still counts).
  float recentMin(float ms) const {
    int n = (int)((ms + WIN * BLOCK_MS) / BLOCK_MS);
    if (n > histN) n = histN;
    float m = 0;
    for (int i = 0; i < n; i++) {
      float v = hist[(histI - 1 - i + RISE_HIST) % RISE_HIST];
      if (i == 0 || v < m) m = v;
    }
    return m;
  }

  static float db(double p) { return 10.0f * log10f((float)p + 1e-15f); }

  static float pct75(const float *v, int n) {
    float t[SECS] = {0};
    if (n <= 0) return -120;
    if (n > SECS) n = SECS;
    for (int i = 0; i < n; i++) t[i] = v[i];
    for (int i = 1; i < n; i++) { float x = t[i]; int j = i - 1; while (j >= 0 && t[j] > x) { t[j + 1] = t[j]; j--; } t[j + 1] = x; }
    return t[(int)(0.75f * (n - 1) + 0.5f)];
  }

  void endBlock() {
    float bassP = blkBass / (BLOCK / DECIM), fullP = blkFull / BLOCK;
    blkBass = blkFull = 0; blkN = 0;
    now += BLOCK_MS;
    S.ms = now;
    secondTick();
    if (skip > 0) { skip--; return; }
    float g = powf(10.0f, -gain / 10.0f);   // take the gain off
    win[winI] = bassP * g; fwin[winI] = fullP * g;
    winI = (winI + 1) % WIN;
    if (winN < WIN) winN++;
    double sb = 0, sf = 0;
    for (int i = 0; i < winN; i++) { sb += win[i]; sf += fwin[i]; }
    float L = db(sb / winN), F = db(sf / winN);
    S.bassDb = L; S.fullDb = F;
    step(L, F, bassP * g, fullP * g);
  }

  // Raw peak and clipping, once a second, and the gain advice.
  void secondTick() {
    if (++secTick < 1000 / BLOCK_MS) return;
    secTick = 0;
    S.peakDb = 20.0f * log10f(secPeak / 32768.0f + 1e-6f);
    S.clipping = secClip > 0;
    bool busy = phase == QUIET || candidate;
    if (secClip > FS / 1000) { advice = -1; quietPeakS = 0; }     // > 0.1% of samples at full scale
    else if (secPeak < 524) {                                        // under -36 dBFS
      if (++quietPeakS >= 20 && !busy) { advice = 1; quietPeakS = 0; }
    } else quietPeakS = 0;
    secPeak = 0; secClip = 0;
  }

  void pushSecond() {
    if (secBlocks == 0) return;
    secs[secI] = db(secBass / secBlocks); fsecs[secI] = db(secFull / secBlocks);
    secI = (secI + 1) % SECS;
    if (nSecs < SECS) nSecs++;
    secBass = secFull = 0; secBlocks = 0;
    base = pct75(secs, nSecs); fullBase = pct75(fsecs, nSecs);
  }

  void step(float L, float F, float bassP, float fullP) {
    float thr = base - P.quietDb;
    bool tooQuiet = base < P.minLevelDb;
    bool silent = F < fullBase - P.silenceDb;
    hist[histI] = L; histI = (histI + 1) % RISE_HIST;
    if (histN < RISE_HIST) histN++;
    bool bassOut = L < thr;
    if (silent || tooQuiet) armed = false;
    else if (!bassOut) armed = true;
    bool quietNow = phase != WARMUP && bassOut && !silent && !tooQuiet && (armed || candidate);

    // Learn the room only while the bass is in (or still warming up).
    bool learning = phase == WARMUP || (phase == LISTEN && !candidate);
    if (learning) {
      secBass += bassP; secFull += fullP;
      if (++secBlocks >= 1000 / BLOCK_MS) pushSecond();
    } else {
      secBass = secFull = 0; secBlocks = 0;
    }

    switch (phase) {
      case WARMUP:
        if (nSecs >= (int)P.warmupS) phase = LISTEN;
        break;

      case LISTEN:
        if (quietNow) {
          if (!candidate) { candidate = true; quietStart = now; bdSum = 0; bdN = 0; }
          aboveSince = 0; lastQuiet = now;
          bdSum += L; bdN++;
          if (now - quietStart >= P.buildMinS * 1000) phase = QUIET;
        } else if (candidate) {
          if (!aboveSince) aboveSince = now;
          if (now - aboveSince > P.graceMs) candidate = false;
        }
        break;

      case QUIET: {
        float bd = bdN ? (float)(bdSum / bdN) : L;
        if (bassOut && !silent) {
          lastQuiet = now; aboveSince = 0; holdRun = 0;
          bdSum += L; bdN++;
          if (now - quietStart > P.buildMaxS * 1000) {   // not a breakdown: the room changed
            phase = WARMUP; candidate = false; nSecs = 0; secI = 0;
          }
          break;
        }
        if (!aboveSince) aboveSince = now;
        bool sharp = now - lastQuiet <= P.riseMs;
        // A drop slams back: JUMP_DB up on the breakdown, and RISE_DB up on
        // where the bass was a moment ago, so a slow sweep back in isn't one.
        bool back = L >= base - P.returnDb && L >= bd + P.jumpDb && L >= recentMin(P.riseMs) + P.riseDb && !silent;
        holdRun = back && sharp ? holdRun + 1 : 0;
        if (holdRun >= HOLD_BLOCKS) {
          fire(L - bd, (lastQuiet - quietStart) / 1000.0f, false);
        } else if (now - lastQuiet > (P.riseMs > P.graceMs ? P.riseMs : P.graceMs)) {
          phase = LISTEN; candidate = false;   // came back slowly, or went silent: not a drop
        }
        break;
      }
    }

    // Gap and surge. A gap only counts with the bass in before it (armed),
    // so a track's first beat after an intro without bass isn't one.
    if (phase != WARMUP && armed && !silent && !tooQuiet && L < base - P.gapDb) {
      if (!gapAt || L < gapL) gapL = L;
      gapAt = now;
    }
    if (gapAt && now - gapAt > P.surgeWinMs) gapAt = 0;
    if (gapAt && phase == LISTEN && now - gapAt >= 1000) {
      float m = recentMean(1000);   // the whole second after the gap
      if (m >= base + P.surgeDb && !silent) fire(m - gapL, 0, true);
    }

    S.phase = phase;
    S.candidate = candidate;
    S.baseDb = base; S.fullBaseDb = fullBase;
    S.tooQuiet = tooQuiet;
    S.quietS = (candidate || phase == QUIET) ? (now - quietStart) / 1000.0f : 0;
    S.warmupFrac = phase == WARMUP ? nSecs / P.warmupS : 1;
    S.cooldownS = now < cooldownUntil ? (cooldownUntil - now) / 1000.0f : 0;
  }
};

} // namespace drop
