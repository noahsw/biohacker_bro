// Host tests for the totem's music tempo tracker (totem/tempo_detect.h):
// synthetic tracks (kick, off-beat bass, hats, pads; breakdowns with snare
// rolls; half-time grooves; crowd noise) go through it as the board feeds it,
// 20 ms at a time. It has to lock on the right whole number, fast, keep it
// through a breakdown, move to the next track's tempo, keep its beat clock on
// the kicks, and show nothing for noise or silence.

#include "../totem/tempo_detect.h"

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static const int FS = tempo::Tracker::FS;
static const float PI = 3.14159265f;

static uint32_t rng = 12345;
static float noise() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) / 8388608.0f) - 1.0f; }

struct Part {
  float seconds;
  float bpm;
  bool kick = true, bass = true, hats = true, pads = true;
  bool halfTime = false;   // kick on beats 1 and 3 only, snare on 2 and 4
  bool snareRoll = false;
  float crowd = 0;         // crowd noise (broadband + random claps), relative to the kick
  bool silent = false;
  float toBpm = 0;         // >0: tempo glides to this over the part (a DJ's transition)
};

// Renders into stereo 16-bit at `level`. Also notes every kick's time.
static std::vector<int16_t> render(const std::vector<Part> &parts, float level, std::vector<double> *kicks = nullptr) {
  std::vector<int16_t> out;
  double t0 = 0, phase = 0;   // phase in beats, so a gliding tempo stays smooth
  float crowdLp = 0;
  float clapAt = -1;
  for (const Part &p : parts) {
    int n = (int)(p.seconds * FS);
    for (int i = 0; i < n; i++) {
      float pt = (float)i / FS;
      float bpm = p.toBpm > 0 ? p.bpm + (p.toBpm - p.bpm) * pt / p.seconds : p.bpm;
      double prev = phase;
      phase += bpm / 60.0 / FS;
      if (kicks && p.kick && !p.silent && floor(phase) != floor(prev)) {
        long b = (long)floor(phase);
        if (!p.halfTime || b % 2 == 0) kicks->push_back(t0 + pt);
      }
      float beat = 60.0f / bpm;
      float bt = (float)(phase - floor(phase)) * beat;                 // seconds into this beat
      float b2 = (float)(fmod(phase, 2.0)) * beat;                       // into this pair of beats
      float s = 0;
      if (p.kick) {
        float kt = p.halfTime ? b2 : bt;
        float f = 50 + 70 * expf(-kt / 0.03f);
        s += 0.55f * expf(-kt / 0.15f) * sinf(2 * PI * f * kt);
      }
      if (p.halfTime) {   // snare on the off pair
        float st = fmodf(b2 + beat, 2 * beat);
        s += 0.25f * noise() * expf(-st / 0.05f);
      }
      if (p.bass && !p.halfTime) {
        float off = fmodf(bt + beat / 2, beat);
        s += 0.30f * (off < beat * 0.45f ? 1.0f : 0.0f) * sinf(2 * PI * 55 * (float)(t0 + pt));
      }
      if (p.hats) {
        float off = fmodf(bt + beat / 2, beat);
        if (off < 0.04f) s += 0.08f * noise() * expf(-off / 0.01f);
      }
      if (p.pads) {
        float t = (float)(t0 + pt);
        s += 0.06f * (sinf(2 * PI * 220 * t) + sinf(2 * PI * 277.2f * t) + sinf(2 * PI * 329.6f * t));
      }
      if (p.snareRoll) {
        float div = pt < p.seconds * 0.4f ? 1 : pt < p.seconds * 0.7f ? 2 : pt < p.seconds * 0.9f ? 4 : 8;
        float sb = fmodf(pt, beat / div);
        s += 0.25f * noise() * expf(-sb / 0.03f);
      }
      if (p.crowd > 0) {
        // Roar: noise with its own low end (people, wind), plus random claps
        // and shouts at no particular tempo.
        crowdLp += 0.05f * (noise() - crowdLp);
        float c = 0.6f * noise() + 2.0f * crowdLp;
        if (clapAt < 0 && noise() > 0.99985f) clapAt = 0;
        if (clapAt >= 0) { c += 1.5f * noise() * expf(-clapAt / 0.02f); clapAt += 1.0f / FS; if (clapAt > 0.2f) clapAt = -1; }
        s += p.crowd * 0.55f * c;
      }
      if (p.silent) s = 0;
      float v = (s * level + 0.0005f * noise()) * 32767.0f;
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      out.push_back((int16_t)v);
      out.push_back((int16_t)(v * 0.95f));
    }
    t0 += p.seconds;
  }
  return out;
}

struct Trace { float t; tempo::Snapshot s; };

// 20 ms reads, as the board does, at a fixed gain. Samples the snapshot every
// 250 ms. gainAt/gainTo: the gain jumps once, mid-way (the auto gain).
static std::vector<Trace> run(const std::vector<int16_t> &s, float gainDb = 0, float gainAt = -1, float gainTo = 0) {
  tempo::Tracker d;
  d.begin(tempo::Params());
  std::vector<Trace> tr;
  const int N = 320;
  std::vector<int16_t> buf(2 * N);
  float g = gainDb; bool changed = false;
  float nextSample = 0;
  for (size_t f = 0; f + N <= s.size() / 2; f += N) {
    float t = (float)f / FS;
    if (gainAt >= 0 && !changed && t >= gainAt) { g = gainTo; changed = true; d.noteGainChange(); }
    float k = powf(10.0f, g / 20.0f);
    for (int i = 0; i < 2 * N; i++) {
      float v = s[2 * f + i] * k;
      buf[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    d.feed(buf.data(), N, g);
    if (t >= nextSample) { tr.push_back({t, d.snapshot()}); nextSample += 0.25f; }
  }
  return tr;
}

// First time it showed `want` (and kept it to the end of [from, to)), or -1.
static float lockedOn(const std::vector<Trace> &tr, int want, float from, float to) {
  float first = -1;
  for (auto &x : tr) {
    if (x.t < from || x.t >= to) continue;
    if (x.s.shown == want) { if (first < 0) first = x.t; }
    else first = -1;
  }
  return first;
}

static int shownAt(const std::vector<Trace> &tr, float t) {
  for (auto &x : tr) if (x.t >= t) return x.s.shown;
  return -1;
}

// Every value shown in [from, to) (0 = nothing).
static void shownSet(const std::vector<Trace> &tr, float from, float to, int &lo, int &hi, int &zeros) {
  lo = 1000; hi = 0; zeros = 0;
  for (auto &x : tr) {
    if (x.t < from || x.t >= to) continue;
    if (x.s.shown == 0) { zeros++; continue; }
    if (x.s.shown < lo) lo = x.s.shown;
    if (x.s.shown > hi) hi = x.s.shown;
  }
}

static void steady(const char *name, float bpm, int want, Part base = Part{0, 0}, float level = 0.3f) {
  base.seconds = 30; base.bpm = bpm;
  auto tr = run(render({base}, level));
  float at = lockedOn(tr, want, 0, 30);
  int lo, hi, z;
  shownSet(tr, 12, 30, lo, hi, z);
  float minConf = 1;
  for (auto &x : tr) if (x.t >= 12 && x.s.conf < minConf) minConf = x.s.conf;
  std::printf("  %s: shows %d from %.2f s (12-30 s: %d..%d, %d blanks, confidence >= %.2f)\n", name, want, at, lo, hi, z, minConf);
  CHECK(at >= 0 && at <= 10, "%s: should show %d within 10 s, got from %.2f", name, want, at);
  CHECK(lo == want && hi == want && z == 0, "%s: should hold %d steadily, got %d..%d with %d blanks", name, want, lo, hi, z);
}

int main() {
  std::printf("test_tempo_detect\n");

  // House and trance, four on the floor.
  steady("house 128", 128, 128);
  steady("house 124", 124, 124);
  steady("trance 138", 138, 138);
  steady("127 isn't 128", 127, 127);
  steady("129 isn't 128", 129, 129);
  steady("slow house 100", 100, 100);
  steady("drum and bass 174", 174, 174);
  { Part p{0, 0}; p.halfTime = true; steady("half-time dubstep 140 (kicks every other beat)", 140, 140, p); }
  steady("downtempo 70 shows as 140", 70, 140);
  { Part p{0, 0}; p.bass = false; p.hats = false; p.pads = false; steady("kick only 132", 132, 132, p); }
  { Part p{0, 0}; p.crowd = 0.5f; steady("128 under a loud crowd", 128, 128, p); }
  steady("128 very quiet (living room, far away)", 128, 128, Part{0, 0}, 0.004f);
  steady("128 clipping (festival, gain too high)", 128, 128, Part{0, 0}, 3.0f);

  // Every whole tempo from 90 to 175: the right number, not a neighbour or
  // a 3/4 or 4/3 cousin.
  {
    int bad = 0;
    for (int v = 0; v < 4; v++)
    for (int b = 90 + v; b <= 175; b += v ? 3 : 1) {   // every tempo for a full mix; every third for the rest
      Part p{20, (float)b};
      if (v == 1) { p.bass = p.hats = p.pads = false; }   // kick only
      if (v == 2) p.halfTime = true;
      if (v == 3) p.crowd = 0.7f;
      auto tr = run(render({p}, 0.3f));
      int lo, hi, z; shownSet(tr, 10, 20, lo, hi, z);
      if (lo != b || hi != b || z) { bad++; std::printf("  sweep %d (mix %d): showed %d..%d, %d blanks\n", b, v, lo, hi, z); }
    }
    std::printf("  sweep 90-175 (full mix, kick only, half-time, crowd): %d wrong\n", bad);
    CHECK(bad == 0, "sweep: %d tempos wrong", bad);
  }
  { Part p{0, 0}; p.crowd = 1.0f; steady("126 with the crowd as loud as the kick", 126, 126, p); }

  // Noise and silence: never a number.
  {
    Part crowd{60, 128}; crowd.kick = crowd.bass = crowd.hats = crowd.pads = false; crowd.crowd = 1;
    auto tr = run(render({crowd}, 0.3f));
    int lo, hi, z; shownSet(tr, 0, 60, lo, hi, z);
    float maxConf = 0; for (auto &x : tr) if (x.s.conf > maxConf) maxConf = x.s.conf;
    std::printf("  crowd only: %d blanks of %d, best confidence %.2f\n", z, (int)tr.size(), maxConf);
    CHECK(hi == 0, "crowd only: should show nothing, showed %d..%d", lo, hi);
  }
  {
    Part pads{60, 128}; pads.kick = pads.bass = pads.hats = false;
    auto tr = run(render({pads}, 0.3f));
    int lo, hi, z; shownSet(tr, 0, 60, lo, hi, z);
    CHECK(hi == 0, "pads only: should show nothing, showed %d..%d", lo, hi);
  }
  {
    Part sil{30, 128}; sil.silent = true;
    auto tr = run(render({sil}, 0.3f));
    int lo, hi, z; shownSet(tr, 0, 30, lo, hi, z);
    CHECK(hi == 0, "silence: should show nothing, showed %d", hi);
  }

  // A trance breakdown: 25 s with no kick (pads, then a snare roll), then the
  // drop. The number stays up all the way through.
  {
    Part groove{30, 138};
    Part down{15, 138}; down.kick = down.bass = down.hats = false;
    Part build{10, 138}; build.kick = build.bass = false; build.snareRoll = true;
    Part drop{20, 138};
    auto tr = run(render({groove, down, build, drop}, 0.3f));
    int lo, hi, z; shownSet(tr, 15, 75, lo, hi, z);
    std::printf("  breakdown: 15-75 s shows %d..%d, %d blanks\n", lo, hi, z);
    CHECK(lo == 138 && hi == 138 && z == 0, "breakdown: should hold 138 throughout, got %d..%d, %d blanks", lo, hi, z);
  }
  // A silence long past the hold: unlocks.
  {
    Part groove{20, 128};
    Part gap{40, 128}; gap.silent = true;
    auto tr = run(render({groove, gap}, 0.3f));
    CHECK(shownAt(tr, 59) == 0, "set over: should go blank, shows %d", shownAt(tr, 59));
  }
  // The next track: 128 for 30 s, an 8 s glide, 140 for 30 s.
  {
    Part a{30, 128}, glide{8, 128}, b{30, 140};
    glide.toBpm = 140;
    auto tr = run(render({a, glide, b}, 0.3f));
    float at = lockedOn(tr, 140, 30, 68);
    std::printf("  128 -> 140: shows 140 from %.2f s (glide ends at 38 s)\n", at);
    CHECK(shownAt(tr, 29) == 128, "track change: 128 before, shows %d", shownAt(tr, 29));
    CHECK(at >= 30 && at <= 46, "track change: should show 140 within 8 s of the glide ending, got %.2f", at);
  }
  // A gain change mid-song: no blip.
  {
    Part a{40, 128};
    auto tr = run(render({a}, 0.05f), 12, 20, 21);
    int lo, hi, z; shownSet(tr, 12, 40, lo, hi, z);
    CHECK(lo == 128 && hi == 128 && z == 0, "gain change: should hold 128, got %d..%d, %d blanks", lo, hi, z);
  }

  // The beat clock: predicted beats land on the kicks.
  {
    for (float bpm : {124.0f, 128.0f, 138.0f}) {
      std::vector<double> kicks;
      Part a{40, bpm};
      auto s = render({a}, 0.3f, &kicks);
      tempo::Tracker d; d.begin(tempo::Params());
      std::vector<int16_t> buf(640);
      double sumErr = 0, maxErr = 0; int n = 0;
      uint32_t seenBeats = 0;
      for (size_t f = 0; f + 320 <= s.size() / 2; f += 320) {
        for (int i = 0; i < 640; i++) buf[i] = s[2 * f + i];
        d.feed(buf.data(), 320, 0);
        const tempo::Snapshot &sn = d.snapshot();
        if (sn.locked && sn.beats != seenBeats && sn.ms > 20000) {
          seenBeats = sn.beats;
          double bt = sn.lastBeatMs / 1000.0, best = 1;
          for (double k : kicks) if (fabs(k - bt) < fabs(best)) best = k - bt;
          sumErr += fabs(best); if (fabs(best) > maxErr) maxErr = fabs(best); n++;
        }
      }
      std::printf("  beat clock at %.0f: %d beats, mean %.0f ms, worst %.0f ms off the kicks\n", bpm, n, 1000 * sumErr / n, 1000 * maxErr);
      CHECK(n > 30 && sumErr / n < 0.025 && maxErr < 0.05, "beat clock at %.0f: mean %.3f s, worst %.3f s", bpm, sumErr / n, maxErr);
    }
  }

  if (failures) { std::printf("%d FAILED\n", failures); return 1; }
  std::printf("all passed\n");
  return 0;
}
