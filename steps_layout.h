#ifndef STEPS_LAYOUT_H
#define STEPS_LAYOUT_H

// ============================================================================
// Geometry for the step count + "STEPS" block (the bottom row of the panel)
// ============================================================================
//
// Split out of display_ui.cpp for one reason: it is the only part of that
// file's layout that is arithmetic rather than judgement, so it is the only
// part a host test can check. display_ui.cpp needs the Adafruit GFX and HUB75
// headers and a live panel; this header needs nothing, so tests/ can include
// it directly and the drawing code and the tests share one definition of the
// layout instead of each carrying its own copy.
//
// Header-only, unlike hr_zones' .h/.cpp pair, so nothing has to be added to
// the sketch build or to the symlink lists in tools/build_main.sh and the
// wokwi test sketches. Six inline functions of integer math don't need a
// translation unit.
//
// Deliberately NOT here: anything that needs a font measured at runtime. The
// label's width comes from GFX's getTextBounds() and is passed in, because
// assuming a TomThumb advance is exactly the kind of by-hand guess that
// printRightAligned() exists to avoid.

// Built-in 5x7 font: 5px glyph + 1px gap.
const int STEPS_DIGIT_ADVANCE = 6;
// The thousands separator: a 2px diagonal tick + 1px gap, in place of the
// font's own 6px comma. See drawStepCount().
const int STEPS_SEP_ADVANCE = 3;
// Blank columns between the count and the word "STEPS". The point of this
// whole header is that this number is the same at every digit count.
const int STEPS_GAP = 2;
// Capped at 5 digits: far beyond a party's worth of walking, and a number
// that sticks degrades more gracefully than one that outgrows its column.
const unsigned long STEPS_MAX = 99999UL;

// Digits in `value` as it will be printed. Matches snprintf("%lu"), including
// value 0 printing as one digit rather than none — which the tests pin,
// because drawStepCount() still gets its actual glyphs from snprintf and the
// two must not disagree about how wide the result is.
inline int stepCountDigits(unsigned long value) {
  int n = 1;
  while (value >= 10) {
    value /= 10;
    n++;
  }
  return n;
}

// How wide drawStepCount() will render `value`, so the count and its label can
// be positioned as one block before either is drawn.
//
// The trailing -1 is the gap after the last digit, which is advance but not
// ink: a glyph is 5px of a 6px cell, so n cells span n*6 - 1 columns.
inline int stepCountWidth(unsigned long value) {
  int n    = stepCountDigits(value);
  int seps = (n - 1) / 3;
  return n * STEPS_DIGIT_ADVANCE - 1 + seps * STEPS_SEP_ADVANCE;
}

// Total width of the count, the gap, and the label together.
inline int stepsBlockWidth(int countW, int labelW) {
  return countW + STEPS_GAP + labelW;
}

// Left column of the centred block.
//
// Clamped at 0 so that a block too wide for the panel degrades to
// left-aligned and merely clips on the right, rather than taking a negative
// cursor into GFX and wrapping the label onto another row — which would look
// like a rendering fault rather than a number that got too big. Same spirit as
// the `if (width < 1) width = 1` floor on the HR bar.
inline int stepsBlockX(int countW, int labelW, int panelW) {
  int x = (panelW - stepsBlockWidth(countW, labelW)) / 2;
  return x < 0 ? 0 : x;
}

// Last column of the count: what drawStepCount() takes as its `rightEdge`.
inline int stepCountRightX(int blockX, int countW) {
  return blockX + countW - 1;
}

// First column of the label, STEPS_GAP clear of the count. The caller still
// subtracts the font's left side bearing before setting the cursor.
inline int stepsLabelX(int blockX, int countW) {
  return blockX + countW + STEPS_GAP;
}

#endif // STEPS_LAYOUT_H
