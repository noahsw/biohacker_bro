// Host tests for totem/music_face.h: who goes on the ring for each mix of
// live readouts, the trio's drift, and the icons.
//
//   make test_music_face    (or just make)

#include "../totem/music_face.h"

#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

using music::Layout;
using music::pick;

static void checkLayout(const char *what, Layout L, music::Mode mode, int n,
                        music::Kind a, music::Kind b = music::YOU, music::Kind c = music::YOU) {
  music::Kind want[3] = {a, b, c};
  bool ok = L.mode == mode && L.n == n;
  for (int i = 0; ok && i < n; i++) ok = L.k[i] == want[i];
  CHECK(ok, "%s: got mode %d n %d [%d %d %d]", what, L.mode, L.n, L.k[0], L.k[1], L.k[2]);
}

static void testPick() {
  using namespace music;
  // you, friend, music, crewFaces
  checkLayout("all three", pick(true, true, true, true), TRIO, 3, YOU, MUSIC, FRIEND);
  checkLayout("you + music", pick(true, false, true, true), PAIR, 2, YOU, MUSIC);
  checkLayout("you + friend (as before)", pick(true, true, false, true), PAIR, 2, YOU, FRIEND);
  checkLayout("friend + music, your strap off", pick(false, true, true, true), PAIR, 2, FRIEND, MUSIC);
  checkLayout("just you", pick(true, false, false, true), ALL, 1, YOU);
  checkLayout("just music", pick(false, false, true, true), ALL, 1, MUSIC);
  checkLayout("just friend", pick(false, true, false, true), ALL, 1, FRIEND);
  checkLayout("nobody: you, searching", pick(false, false, false, true), ALL, 1, YOU);
  // Friends off the faces: a friend only shows when you're gone.
  checkLayout("crew off, all live", pick(true, true, true, false), PAIR, 2, YOU, MUSIC);
  checkLayout("crew off, you + friend", pick(true, true, false, false), ALL, 1, YOU);
  checkLayout("crew off, you gone", pick(false, true, true, false), PAIR, 2, FRIEND, MUSIC);
  CHECK(pick(true, true, true, true) != pick(true, false, true, true), "layouts compare unequal");
  CHECK(pick(true, false, true, true) == pick(true, false, true, true), "layouts compare equal");
}

static void testTrio() {
  const int cols = 64;
  // At every step of a lap, each tile shows each of its 16 columns with full
  // weight somewhere (the blend sums to one), and no column belongs to two.
  for (float off = 0; off < cols; off += 0.25f) {
    float weight[3][16] = {};
    int lit = 0;
    for (int c = 0; c < cols; c++) {
      int u0; float fr;
      int t = music::trioAt(off, c, cols, u0, fr);
      if (t < 0) continue;
      CHECK(u0 >= -1 && u0 <= 15 && fr >= 0 && fr < 1, "off %.2f col %d: u0 %d frac %.2f", off, c, u0, fr);
      if (u0 >= 0) weight[t][u0] += 1 - fr;
      if (u0 + 1 <= 15) weight[t][u0 + 1] += fr;
      lit++;
    }
    for (int t = 0; t < 3; t++)
      for (int u = 0; u < 16; u++)
        CHECK(std::fabs(weight[t][u] - 1) < 1e-4f, "off %.2f tile %d col %d weight %.3f", off, t, u, weight[t][u]);
    CHECK(lit <= 51, "off %.2f: %d lit columns", off, lit);
  }
  // Whole-column offset: tile 0 sits exactly at its offset.
  int u0; float fr;
  CHECK(music::trioAt(10, 10, cols, u0, fr) == 0 && u0 == 0 && fr == 0, "tile 0 starts at offset");
  CHECK(music::trioAt(0, 63, cols, u0, fr) == -1 || u0 == -1, "column before tile 0 is dark or its edge");
  CHECK(std::fabs(music::trioOffset(32000, 2.0f, cols) - 0.0f) < 1e-3f, "one lap in 32 s at 2 px/s");
  CHECK(std::fabs(music::trioOffset(1500, 2.0f, cols) - 3.0f) < 1e-3f, "3 px after 1.5 s");
}

static void testIcons() {
  int note = 0, spk = 0;
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      note += music::iconAt(0, x, y);
      spk += music::iconAt(1, x, y);
      bool inBox = x >= music::ICON_X && x < music::ICON_X + music::ICON_W &&
                   y >= music::ICON_Y && y < music::ICON_Y + music::ICON_H;
      if (!inBox) CHECK(!music::iconAt(0, x, y) && !music::iconAt(1, x, y), "icon pixel outside box at %d,%d", x, y);
    }
  CHECK(note > 10 && spk > 10, "icons have pixels (note %d, speaker %d)", note, spk);
  CHECK(music::beatBarFill(0) == 4 && music::beatBarFill(3) == 16 && music::beatBarFill(4) == 4, "beat bar 4-8-12-16");
}

int main() {
  testPick();
  testTrio();
  testIcons();
  if (failures) { std::printf("test_music_face: %d failed\n", failures); return 1; }
  std::printf("test_music_face: all passed\n");
  return 0;
}
