#pragma once

// ============================================================================
// RING LAYOUT (48x16) — pure, no Arduino, no FastLED: tested under tests/
// ============================================================================
//
// The round totem: three 16x16 panels wrapped LEDs-out round a printed core,
// making one 48-column ring. Column 0..47 runs left to right as you look at
// the ring from outside.
//
//   rows 0..8    EKG trace of your heart rate: a write head sweeps round the
//                ring and draws one heartbeat spike per real beat, in your
//                zone colour, fading behind it like a hospital monitor
//   row  9       gap
//   rows 10..15  ticker drifting round the ring:
//                  <heart> 72   DON'T DIE   V 88   DON'T DIE   K 95   DON'T DIE ...
//                your BPM after a pulsing heart, each friend's BPM after their
//                initial, "DON'T DIE" between every pair
//
// Mirrors 3d-prints/edm-totem/source/render_ekg.py, which drew the renders.

#include <stdint.h>
#include <string.h>
#include "face_layout.h"

namespace ring {

const int COLS = 48, ROWS = 16;

// --- EKG ---
// Pixels above the baseline for each step after a beat: P wave, Q dip, R spike,
// S dip, T wave. One step per column at EKG_PX_PER_S (totem_config.h).
static const int8_t WAVE[] = {1, 1, 0, -1, 6, -2, 0, 0, 1, 2, 1};
const int WAVE_LEN = sizeof(WAVE) / sizeof(WAVE[0]);
const int BASE_ROW = 6;        // baseline; the R spike reaches row 0, the S dip row 8
const int GAP_AHEAD = 4;       // blank columns in front of the write head

inline int waveAt(int step) { return step >= 0 && step < WAVE_LEN ? WAVE[step] : 0; }
inline int waveRow(int y) { return BASE_ROW - y; }

// --- Ticker font: 6 rows, variable width, 1 px between glyphs ---
const int TEXT_Y = 10, TEXT_H = 6;
const char HEART_CH = '<';     // stands for the little heart before your BPM

struct Glyph { char c; const char *rows[TEXT_H]; };

static const Glyph FONT[] = {
  {'<', {".X.X.", "XXXXX", "XXXXX", ".XXX.", "..X..", "....."}},
  {' ', {"..", "..", "..", "..", "..", ".."}},
  {'\'', {"X", "X", ".", ".", ".", "."}},
  {'A', {".XX.", "X..X", "X..X", "XXXX", "X..X", "X..X"}},
  {'B', {"XXX.", "X..X", "XXX.", "X..X", "X..X", "XXX."}},
  {'C', {".XXX", "X...", "X...", "X...", "X...", ".XXX"}},
  {'D', {"XXX.", "X..X", "X..X", "X..X", "X..X", "XXX."}},
  {'E', {"XXX", "X..", "XX.", "X..", "X..", "XXX"}},
  {'F', {"XXX", "X..", "XX.", "X..", "X..", "X.."}},
  {'G', {".XXX", "X...", "X...", "X.XX", "X..X", ".XXX"}},
  {'H', {"X..X", "X..X", "XXXX", "X..X", "X..X", "X..X"}},
  {'I', {"XXX", ".X.", ".X.", ".X.", ".X.", "XXX"}},
  {'J', {"...X", "...X", "...X", "...X", "X..X", ".XX."}},
  {'K', {"X..X", "X.X.", "XX..", "X.X.", "X..X", "X..X"}},
  {'L', {"X..", "X..", "X..", "X..", "X..", "XXX"}},
  {'M', {"X...X", "XX.XX", "X.X.X", "X...X", "X...X", "X...X"}},
  {'N', {"X..X", "XX.X", "X.XX", "X..X", "X..X", "X..X"}},
  {'O', {".XX.", "X..X", "X..X", "X..X", "X..X", ".XX."}},
  {'P', {"XXX.", "X..X", "X..X", "XXX.", "X...", "X..."}},
  {'Q', {".XX.", "X..X", "X..X", "X..X", "X.X.", ".X.X"}},
  {'R', {"XXX.", "X..X", "X..X", "XXX.", "X.X.", "X..X"}},
  {'S', {".XXX", "X...", ".XX.", "...X", "...X", "XXX."}},
  {'T', {"XXX", ".X.", ".X.", ".X.", ".X.", ".X."}},
  {'U', {"X..X", "X..X", "X..X", "X..X", "X..X", ".XX."}},
  {'V', {"X...X", "X...X", "X...X", ".X.X.", ".X.X.", "..X.."}},
  {'W', {"X...X", "X...X", "X...X", "X.X.X", "XX.XX", "X...X"}},
  {'X', {"X..X", "X..X", ".XX.", ".XX.", "X..X", "X..X"}},
  {'Y', {"X.X", "X.X", "X.X", ".X.", ".X.", ".X."}},
  {'Z', {"XXXX", "...X", "..X.", ".X..", "X...", "XXXX"}},
};
const int FONT_LEN = sizeof(FONT) / sizeof(FONT[0]);

// Rows of a character, digits and '-' from the face font so both totems match.
// Returns the glyph width, or 0 for a character the font doesn't have.
inline int glyphRows(char c, const char *rows[TEXT_H]) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  if ((c >= '0' && c <= '9') || c == '-') {
    int g = c == '-' ? 10 : c - '0';
    for (int r = 0; r < TEXT_H; r++) rows[r] = face::GLYPH[g][r];
    return face::DIGIT_W;
  }
  for (int i = 0; i < FONT_LEN; i++)
    if (FONT[i].c == c) {
      for (int r = 0; r < TEXT_H; r++) rows[r] = FONT[i].rows[r];
      return (int)strlen(FONT[i].rows[0]);
    }
  return 0;
}

// --- Ticker ---
// One column of the ticker strip: which of the 6 rows are lit (bit 0 = top),
// whose segment it belongs to and what kind, so the sketch can colour it.
enum Kind : uint8_t { BLANK = 0, BADGE = 1, RATE = 2, SLOGAN = 3 };
struct Col { uint8_t bits; uint8_t kind; int8_t person; };

struct Person {
  char initial;   // ignored for person 0 (you), who gets the heart instead
  int bpm;        // <= 0 when not connected
  bool live;      // connected and sending; friends who aren't live are left out
};

const char *const SLOGAN_TEXT = "DON'T DIE";
const int SEG_GAP = 5;          // blank columns between a rate and the slogan
// The ticker is longer than the ring, so somewhere new text has to meet old.
// Four always-dark columns there (at the panel seam between 47 and 0) make it
// read as text sliding out of a slot rather than "DIE" running into "74".
const int SEAM_COLS = 4;
const int MAX_COLS = 400;       // you + 4 friends need ~320

// Appends text at out[n...], returns the new length (stops at maxCols).
inline int appendText(const char *s, uint8_t kind, int8_t person, Col *out, int n, int maxCols) {
  for (int i = 0; s[i]; i++) {
    const char *rows[TEXT_H];
    int w = glyphRows(s[i], rows);
    if (i > 0 && s[i - 1] != ' ' && s[i] != ' ') {   // 1 px between glyphs, not around spaces
      if (n >= maxCols) return n;
      out[n++] = Col{0, kind, person};
    }
    for (int c = 0; c < w; c++) {
      if (n >= maxCols) return n;
      uint8_t bits = 0;
      for (int r = 0; r < TEXT_H; r++)
        if (rows[r][c] == 'X') bits |= (uint8_t)(1 << r);
      out[n++] = Col{bits, kind, person};
    }
  }
  return n;
}

inline int appendGap(int w, Col *out, int n, int maxCols) {
  for (int i = 0; i < w && n < maxCols; i++) out[n++] = Col{0, BLANK, -1};
  return n;
}

// Builds the ticker loop. Person 0 (you) is always there, "--" when the Whoop
// isn't connected; friends appear only while live. Returns the loop length.
inline int buildTicker(const Person *p, int n, Col *out, int maxCols) {
  int len = 0;
  for (int i = 0; i < n; i++) {
    if (i > 0 && !p[i].live) continue;
    char badge[2] = {i == 0 ? HEART_CH : p[i].initial, 0};
    char rate[4] = "--";
    int bpm = p[i].live ? p[i].bpm : 0;
    if (bpm >= 10 && bpm <= 999) {
      int k = bpm >= 100 ? 3 : 2;
      rate[k] = 0;
      for (int j = k - 1; j >= 0; j--) { rate[j] = (char)('0' + bpm % 10); bpm /= 10; }
    }
    len = appendText(badge, BADGE, (int8_t)i, out, len, maxCols);
    len = appendGap(2, out, len, maxCols);
    len = appendText(rate, RATE, (int8_t)i, out, len, maxCols);
    len = appendGap(SEG_GAP, out, len, maxCols);
    len = appendText(SLOGAN_TEXT, SLOGAN, -1, out, len, maxCols);
    len = appendGap(SEG_GAP, out, len, maxCols);
  }
  return len;
}

} // namespace ring
