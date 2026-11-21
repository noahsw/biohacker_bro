// Host tests for the totem's beat drop detector (totem/drop_detect.h): a
// synthetic trance track (kick, bass, hats, pads; breakdowns with snare rolls
// and risers; drops) goes through it at living-room and festival levels, and
// it has to fire on the drops, at the drops, and nowhere else: not on a short
// gap, not on silence between sets, not on a slow filter sweep back in, not on
// a gain change. Also the other drop shape (a short gap, then bass louder than
// the groove), and the gain advice.

#include "../totem/drop_detect.h"
#include "../totem/drop_fx.h"

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static const int FS = drop::Detector::FS;
static const float PI = 3.14159265f;

static uint32_t rng = 12345;
static float noise() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) / 8388608.0f) - 1.0f; }

// What's playing in a stretch of the track.
struct Part {
  float seconds;
  bool kick, bass, hats, pads, snareRoll, riser, silent;
  float lowpassSweep = 0;   // >0: kick+bass fade in through a low-pass opening over this part (no drop)
  bool fadeIn = false;      // the whole part fades up from 30 dB down (a set starting, the volume coming up)
  float bassGain = 1;       // kick and bass this many times louder (a drop that's louder than the groove)
};

// Renders the track into stereo 16-bit at `level` (1 = loud), with a little
// room noise so silence isn't digital zero.
static std::vector<int16_t> render(const std::vector<Part> &parts, float level, std::vector<float> *partStart = nullptr) {
  std::vector<int16_t> out;
  const float beat = 60.0f / 128.0f;
  double t0 = 0;
  for (const Part &p : parts) {
    if (partStart) partStart->push_back((float)t0);
    int n = (int)(p.seconds * FS);
    float lpState = 0;
    for (int i = 0; i < n; i++) {
      double t = t0 + (double)i / FS;
      float pt = (float)i / FS;     // time into the part
      float bt = fmodf((float)t, beat);
      float s = 0;
      float bassAmt = p.bassGain;
      if (p.lowpassSweep > 0) bassAmt *= pt / p.seconds;   // slow fade in
      if (p.kick) {
        float f = 50 + 70 * expf(-bt / 0.03f);
        s += 0.55f * bassAmt * expf(-bt / 0.15f) * sinf(2 * PI * f * bt);
      }
      if (p.bass) {
        float off = fmodf((float)t + beat / 2, beat);
        s += 0.30f * bassAmt * (off < beat * 0.45f ? 1.0f : 0.0f) * sinf(2 * PI * 55 * (float)t);
      }
      if (p.hats) {
        float off = fmodf((float)t + beat / 2, beat);
        if (off < 0.04f) s += 0.08f * noise() * expf(-off / 0.01f);
      }
      if (p.pads) s += 0.06f * (sinf(2 * PI * 220 * (float)t) + sinf(2 * PI * 277.2f * (float)t) + sinf(2 * PI * 329.6f * (float)t));
      if (p.snareRoll) {
        // Rolls speed up through the part: quarters, eighths, sixteenths, 32nds.
        float div = pt < p.seconds * 0.4f ? 1 : pt < p.seconds * 0.7f ? 2 : pt < p.seconds * 0.9f ? 4 : 8;
        float sb = fmodf(pt, beat / div);
        s += 0.25f * noise() * expf(-sb / 0.03f);
      }
      if (p.riser) {
        float f = 300 + 1700 * pt / p.seconds;
        s += 0.10f * (pt / p.seconds) * sinf(2 * PI * f * pt);
      }
      if (p.fadeIn) s *= powf(10.0f, -1.5f * (1 - pt / p.seconds));
      if (p.silent) s = 0;
      lpState = s;
      float room = 0.0005f * noise();
      float v = (lpState * level + room) * 32767.0f;
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      out.push_back((int16_t)v);
      out.push_back((int16_t)(v * 0.95f));
    }
    t0 += p.seconds;
  }
  return out;
}

struct Fired { float at; float jump, build; };

// Runs the samples through in 20 ms reads, as the board does. gainSchedule:
// at time t (s) the recording gain jumps to g dB (the samples are scaled to
// match, as the real codec would).
static std::vector<Fired> run(const std::vector<int16_t> &s, float gainDb = 0, float gainChangeAt = -1,
                              float gainChangeTo = 0, drop::Detector *out = nullptr) {
  drop::Detector d;
  d.begin(drop::Params());
  std::vector<Fired> fired;
  uint32_t seen = 0;
  const int N = drop::Detector::BLOCK;
  std::vector<int16_t> buf(2 * N);
  float g = gainDb;
  bool changed = false;
  for (size_t f = 0; f + N <= s.size() / 2; f += N) {
    float t = (float)f / FS;
    if (gainChangeAt >= 0 && !changed && t >= gainChangeAt) { g = gainChangeTo; changed = true; d.noteGainChange(); }
    float k = powf(10.0f, g / 20.0f);
    for (int i = 0; i < 2 * N; i++) {
      float v = s[2 * f + i] * k;
      buf[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    d.feed(buf.data(), N, g);
    if (d.snapshot().drops != seen) {
      seen = d.snapshot().drops;
      fired.push_back({d.snapshot().ms / 1000.0f, d.snapshot().lastJumpDb, d.snapshot().lastBuildS});
    }
  }
  if (out) *out = d;
  return fired;
}

static void expectDrops(const char *name, const std::vector<Fired> &got, const std::vector<float> &want) {
  CHECK(got.size() == want.size(), "%s: %d drops, expected %d", name, (int)got.size(), (int)want.size());
  for (size_t i = 0; i < got.size() && i < want.size(); i++)
    CHECK(got[i].at >= want[i] && got[i].at < want[i] + 0.8f, "%s: drop %d at %.2f s, the drop is at %.2f s",
          name, (int)i, got[i].at, want[i]);
  for (size_t i = 0; i < got.size(); i++)
    std::printf("  %s: drop at %.2f s (bass +%.1f dB after %.1f s out)\n", name, got[i].at, got[i].jump, got[i].build);
}

int main() {
  // The track. Drops are at the start of parts 3 and 7.
  std::vector<Part> track = {
    {40, true, true, true, true, false, false, false},     // 0 intro, full groove
    {30, false, false, false, true, false, false, false},  // 1 breakdown: pads only
    {14, false, false, true, true, true, true, false},     // 2 build: snare roll + riser
    {60, true, true, true, true, false, false, false},     // 3 DROP
    {1.5f, false, false, true, true, false, false, false}, // 4 a 1.5 s gap: too short
    {30, true, true, true, true, false, false, false},     // 5
    {20, false, false, false, true, false, false, false},  // 6 second breakdown...
    {8, false, false, true, true, true, true, false},      //   ...and build
    {45, true, true, true, true, false, false, false},     // 7 DROP
    {12, false, false, false, false, false, false, true},  // 8 silence between sets
    {40, true, true, true, true, false, false, false},     // 9 next set starts: not a drop
    {15, false, false, false, true, false, false, false},  // 10 breakdown...
    {10, true, true, true, true, false, false, false, 10}, // 11 ...bass fades in slowly: not a drop
    {30, true, true, true, true, false, false, false},     // 12
  };
  std::vector<float> starts;
  std::vector<int16_t> loud = render(track, 0.9f, &starts);
  std::vector<float> dropsAt = {starts[3], starts[8]};

  std::printf("drop detector\n");
  expectDrops("festival level", run(loud), dropsAt);
  // A living room: 30 dB quieter, recorded with 24 dB more gain.
  std::vector<int16_t> room = render(track, 0.9f * 0.0316f);
  expectDrops("living room, +24 dB gain", run(room, 24), dropsAt);
  // Same room with no gain: very quiet, still self-calibrates.
  expectDrops("living room, 0 dB gain", run(room, 0), dropsAt);
  // Gain turned down 6 dB in the middle of the first drop's groove and up
  // 9 dB in the intro of the next set: neither looks like a drop.
  expectDrops("gain change mid-groove", run(room, 18, starts[3] + 20, 12), dropsAt);
  expectDrops("gain change in a groove", run(room, 12, starts[9] + 10, 21), dropsAt);

  // Cooldown: two drops 24 s apart, only the first counts.
  std::vector<Part> quick = {
    {40, true, true, true, true, false, false, false},
    {10, false, false, false, true, false, false, false},
    {6, false, false, true, true, true, true, false},
    {10, true, true, true, true, false, false, false},
    {10, false, false, false, true, false, false, false},
    {4, false, false, true, true, true, true, false},
    {20, true, true, true, true, false, false, false},
  };
  std::vector<float> qs;
  std::vector<int16_t> q = render(quick, 0.9f, &qs);
  expectDrops("cooldown", run(q), {qs[3]});

  // Silence, then the music fades up with the beat still to come, then the
  // kick lands: the bass was never in, so it isn't a breakdown or a drop.
  {
    std::vector<Part> fade = {
      {40, true, true, true, true, false, false, false},
      {12, false, false, false, false, false, false, true},
      {8, false, false, true, true, true, true, false, 0, true},
      {30, true, true, true, true, false, false, false},
    };
    expectDrops("fade up from silence", run(render(fade, 0.9f)), {});
    expectDrops("fade up from silence, living room", run(render(fade, 0.9f * 0.0316f), 24), {});
  }

  // A breakdown with one hit of the groove in it, then the breakdown goes
  // on: that hit isn't a drop.
  {
    std::vector<Part> hit = {
      {40, true, true, true, true, false, false, false},
      {6, false, false, false, true, false, false, false},
      {0.25f, true, true, true, true, false, false, false},
      {8, false, false, false, true, false, false, false},
    };
    expectDrops("lone hit in a breakdown", run(render(hit, 0.9f)), {});
  }

  // Gap and surge: the build keeps some bass, it cuts out for half a second,
  // then comes back 8 dB louder than the groove. Fires once, about a second
  // after the gap. Without the gap, or without the surge, it doesn't.
  {
    const float G = 0.56f, LOUD = 2.5f;   // -5 dB, +8 dB
    std::vector<Part> surge = {
      {40, true, true, true, true, false, false, false},
      {4, true, true, true, true, false, false, false, 0, false, G},   // the build: bass a bit down
      {0.5f, false, false, true, true, false, false, false},           // the gap
      {20, true, true, true, true, false, false, false, 0, false, LOUD},
    };
    std::vector<float> st;
    std::vector<int16_t> sr = render(surge, 0.5f, &st);
    for (float gain : {0.0f, 24.0f}) {
      std::vector<int16_t> lvl = gain > 0 ? render(surge, 0.5f * 0.0316f) : sr;
      std::vector<Fired> f = run(lvl, gain);
      const char *name = gain > 0 ? "gap and surge, living room" : "gap and surge";
      CHECK(f.size() == 1, "%s: %d drops, expected 1", name, (int)f.size());
      if (f.size() == 1) {
        CHECK(f[0].at > st[3] + 0.8f && f[0].at < st[3] + 2.0f, "%s: drop at %.2f s, the gap ends at %.2f s",
              name, f[0].at, st[3]);
        std::printf("  %s: drop at %.2f s (gap ended at %.2f s)\n", name, f[0].at, st[3]);
      }
    }
    std::vector<Part> noGap = {
      {40, true, true, true, true, false, false, false},
      {20, true, true, true, true, false, false, false, 0, false, LOUD},   // a louder chorus, no gap
    };
    expectDrops("louder chorus, no gap", run(render(noGap, 0.5f)), {});
    std::vector<Part> noSurge = {
      {40, true, true, true, true, false, false, false},
      {0.5f, false, false, true, true, false, false, false},
      {20, true, true, true, true, false, false, false},                 // back at the usual level
    };
    expectDrops("gap, no surge", run(render(noSurge, 0.5f)), {});
    std::vector<Part> intro = {
      {40, true, true, true, true, false, false, false},
      {12, false, false, false, false, false, false, true},              // silence between tracks
      {10, false, false, true, true, false, false, false},               // intro, no bass yet
      {20, true, true, true, true, false, false, false, 0, false, LOUD}, // the beat comes in loud
    };
    expectDrops("new track, loud beat after an intro", run(render(intro, 0.5f)), {});
  }

  // Warm-up: a drop in the first 20 s can't be judged yet.
  std::vector<Part> early = {
    {6, true, true, true, true, false, false, false},
    {8, false, false, false, true, false, false, false},
    {20, true, true, true, true, false, false, false},
  };
  expectDrops("warm-up", run(render(early, 0.9f)), {});

  // Quiet room: nothing playing, never fires and says it's too quiet.
  {
    std::vector<Part> hush = {{60, false, false, false, false, false, false, true}};
    drop::Detector d;
    std::vector<Fired> f = run(render(hush, 1.0f), 0, -1, 0, &d);
    CHECK(f.empty(), "silence fired a drop");
    CHECK(d.snapshot().tooQuiet, "silence not reported as too quiet (baseline %.1f dB)", d.snapshot().baseDb);
  }

  // Gain advice: clipping asks for less, a near-silent minute asks for more.
  {
    drop::Detector d;
    d.begin(drop::Params());
    std::vector<int16_t> buf(2 * FS);
    for (int i = 0; i < FS; i++) buf[2 * i] = buf[2 * i + 1] = (i % 40 < 20) ? 32767 : -32768;
    d.feed(buf.data(), FS, 0);
    d.feed(buf.data(), 400, 0);
    CHECK(d.gainAdvice() == -1, "clipping didn't ask for less gain");
    CHECK(d.gainAdvice() == 0, "advice not consumed");
    for (int i = 0; i < 2 * FS; i++) buf[i] = (int16_t)(100 * noise());
    int adv = 0;
    for (int s = 0; s < 25; s++) { d.feed(buf.data(), FS, 0); int a = d.gainAdvice(); if (a) adv = a; }
    CHECK(adv == 1, "a quiet 25 s didn't ask for more gain");
  }

  // The burst: lights the whole ring at once, then lets go, and is over on time.
  {
    static dropfx::Frame f;
    for (int y = 0; y < dropfx::ROWS; y++) for (int x = 0; x < dropfx::COLS; x++) f[y][x] = {0, 40, 0};
    CHECK(dropfx::burst(10, f), "burst over at 10 ms");
    int lit = 0;
    for (int y = 0; y < dropfx::ROWS; y++) for (int x = 0; x < dropfx::COLS; x++) lit += f[y][x].r > 200 && f[y][x].g > 200 && f[y][x].b > 200;
    CHECK(lit == dropfx::ROWS * dropfx::COLS, "flash lit %d of %d", lit, dropfx::ROWS * dropfx::COLS);
    bool anyAfter = false;
    for (uint32_t t = 100; t < dropfx::BURST_MS; t += 40) {
      for (int y = 0; y < dropfx::ROWS; y++) for (int x = 0; x < dropfx::COLS; x++) f[y][x] = {0, 40, 0};
      anyAfter |= dropfx::burst(t, f);
    }
    CHECK(anyAfter, "burst stopped right after the flash");
    for (int y = 0; y < dropfx::ROWS; y++) for (int x = 0; x < dropfx::COLS; x++) f[y][x] = {0, 40, 0};
    CHECK(!dropfx::burst(dropfx::BURST_MS, f), "burst still going at %u ms", (unsigned)dropfx::BURST_MS);
    CHECK(f[3][3].g == 40, "base not left alone after the burst");
  }

  // The meter draws every state without going off the frame.
  {
    static dropfx::Frame f;
    dropfx::Meter m;
    drop::Snapshot s;
    for (int i = 0; i < 40; i++) {
      s.phase = (drop::Phase)(i % 3); s.candidate = i % 5 == 0; s.bassDb = -40 + i; s.baseDb = -30;
      s.drops = i / 10; s.peakDb = -50 + i; s.clipping = i == 39;
      m.push(s);
      dropfx::meter(m, s, 9, 3, 1000 * i, true, f);
    }
    CHECK(true, "meter");
  }

  if (failures) std::printf("drop detector: %d FAILED\n", failures);
  else std::printf("drop detector: all passed\n");
  return failures ? 1 : 0;
}
