// ============================================================================
// Tests for the steps-row geometry (steps_layout.h)
// ============================================================================
//
// Run with `make -C tests`. Same house style as test_hr_zones.cpp: no
// framework, and PROPERTY tests rather than a table of expected pixel columns.
//
// What these can and cannot do is worth being clear about. They cannot tell
// you the row LOOKS right — the bugs on this project have all been perceptual
// or electrical, and were found by looking at the panel, which is why there is
// deliberately no test of the display as a whole. What they can do is pin the
// three things about this row that can be wrong WITHOUT looking obviously
// wrong, or that would look wrong only at a step count nobody has walked to
// yet:
//
//   1. The gap between the count and the word is exactly STEPS_GAP at every
//      digit count. This is the actual spec for the block — "always the same
//      two pixels" — and it is invisible until you happen to compare a
//      4-digit reading against a 5-digit one hours later.
//   2. stepCountWidth() agrees with the advance that drawStepCount()'s loop
//      actually walks. These are two expressions of one layout, and if they
//      drift the whole block sits off-centre by the difference.
//   3. The block stays on the panel. At today's cap it has room to spare, so
//      this is a guard for the next person who raises STEPS_MAX or picks a
//      wider label font — the failure mode is a negative cursor and a label
//      wrapped onto another row, which reads as a broken display.

#include "../steps_layout.h"

// For PANEL_WIDTH, so widening the panel re-checks this row rather than
// leaving the tests asserting against a 64px panel that no longer exists.
// config.h is all #defines and pulls in no Arduino headers; the Makefile
// supplies secrets.h from the example when a real one isn't present.
#include "../config.h"

#include <cstdarg>
#include <cstdio>

// The measured width of "STEPS" in TomThumb, as GFX's getTextBounds() reports
// it at the time of writing: 5 glyphs at a 4px advance. NOT what the firmware
// uses — display_ui.cpp measures the string at runtime precisely so that a
// font change can't be silently wrong. It's here only to give the sweeps below
// a realistic number to work with.
static const int LABEL_W_TOMTHUMB = 20;

static int failures = 0;
static int checks = 0;

static void checkf(bool ok, const char *fmt, ...) {
  checks++;
  if (!ok) {
    failures++;
    printf("  FAIL: ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
  }
}

// Every step count that can reach the panel, plus the boundaries either side
// of each digit and separator transition, which is where the arithmetic breaks
// if it breaks at all.
static void forEachInterestingValue(void (*fn)(unsigned long)) {
  const unsigned long edges[] = {0,     1,     9,     10,    11,     99,
                                 100,   101,   999,   1000,  1001,   9999,
                                 10000, 10001, 99998, 99999, 100000, 4294967295UL};
  for (unsigned long v : edges) fn(v);
  for (unsigned long v = 0; v <= 2000; v++) fn(v);
  for (unsigned long v = 0; v <= STEPS_MAX; v += 137) fn(v);
}

// --- stepCountDigits must agree with snprintf, because drawStepCount() gets
// its glyphs from snprintf while its width comes from here. A disagreement
// would right-align the count against a width it doesn't have.
static void check_digits_match_printf(unsigned long v) {
  char buf[24];
  int n = snprintf(buf, sizeof(buf), "%lu", v);
  checkf(stepCountDigits(v) == n, "%lu: stepCountDigits says %d, printf wrote %d",
         v, stepCountDigits(v), n);
}

static void test_digits_match_printf() {
  printf("digit count agrees with snprintf(\"%%lu\")\n");
  forEachInterestingValue(check_digits_match_printf);
}

// --- The width formula must match the advance drawStepCount()'s loop walks.
//
// This reimplements that loop's stepping — and ONLY its stepping — so the
// closed form and the iteration are checked against each other. If you change
// one, this fails until you change the other, which is the entire point of it.
static void check_width_matches_the_drawing_loop(unsigned long v) {
  char digits[24];
  int n = snprintf(digits, sizeof(digits), "%lu", v);

  int x       = 0;   // pen position, as in drawStepCount
  int lastInk = -1;  // rightmost column a glyph actually touches
  for (int i = 0; i < n; i++) {
    if (i > 0 && (n - i) % 3 == 0) x += STEPS_SEP_ADVANCE;
    lastInk = x + 4;  // the built-in glyph is 5px of its 6px cell: x..x+4
    x += STEPS_DIGIT_ADVANCE;
  }

  checkf(stepCountWidth(v) == lastInk + 1,
         "%lu: stepCountWidth says %d, the drawing loop inks %d columns", v,
         stepCountWidth(v), lastInk + 1);
}

static void test_width_matches_the_drawing_loop() {
  printf("stepCountWidth matches what drawStepCount actually advances\n");
  forEachInterestingValue(check_width_matches_the_drawing_loop);
}

// --- Width must never shrink as the number grows. A count that got NARROWER
// crossing a thousands boundary would slide the whole block sideways in the
// wrong direction.
static void test_width_is_monotonic() {
  printf("width never shrinks as the count rises\n");
  int prev = stepCountWidth(0);
  for (unsigned long v = 1; v <= STEPS_MAX; v++) {
    int w = stepCountWidth(v);
    checkf(w >= prev, "%lu: width dropped from %d to %d", v, prev, w);
    prev = w;
  }
}

// --- THE SPEC: exactly two blank columns between the count's last inked
// column and the label's first, at every digit count and every label width.
//
// Asserted against a LITERAL 2, not against STEPS_GAP. Checking the layout
// against the same constant the layout is built from passes no matter what that
// constant says — the first version of this test did exactly that, and edited
// STEPS_GAP to 1 without a single failure. Two pixels is the requirement; the
// constant is one implementation of it, so the requirement is what's written
// here. If the gap is ever deliberately changed, this number changes with it,
// by hand, which is the point: it's a decision, not a detail.
static const int REQUIRED_GAP = 2;

static void test_gap_is_always_exactly_two() {
  printf("the count-to-label gap is exactly %d at every digit count\n",
         REQUIRED_GAP);
  checkf(STEPS_GAP == REQUIRED_GAP, "STEPS_GAP is %d, but the row calls for %d",
         STEPS_GAP, REQUIRED_GAP);
  for (int labelW = 8; labelW <= 28; labelW++) {
    for (unsigned long v = 0; v <= STEPS_MAX; v += 97) {
      int countW = stepCountWidth(v);
      int blockX = stepsBlockX(countW, labelW, PANEL_WIDTH);
      int gap    = stepsLabelX(blockX, countW) - (stepCountRightX(blockX, countW) + 1);
      checkf(gap == REQUIRED_GAP,
             "%lu with a %dpx label: gap is %dpx, not %d", v, labelW, gap,
             REQUIRED_GAP);
    }
  }
}

// --- The block must fit on the panel, and be centred within the pixel that
// integer division costs.
static void test_block_stays_on_the_panel_and_is_centred() {
  printf("the block fits the panel and is centred to within 1px\n");
  for (unsigned long v = 0; v <= STEPS_MAX; v += 41) {
    int countW = stepCountWidth(v);
    int blockW = stepsBlockWidth(countW, LABEL_W_TOMTHUMB);
    int blockX = stepsBlockX(countW, LABEL_W_TOMTHUMB, PANEL_WIDTH);

    checkf(blockX >= 0, "%lu: block starts at x=%d, off the left edge", v,
           blockX);
    checkf(blockX + blockW <= PANEL_WIDTH,
           "%lu: block is %dpx at x=%d, past the %dpx panel", v, blockW, blockX,
           PANEL_WIDTH);

    // Left and right margins may differ by 1 — the block and the panel can't
    // both be even — but no more than that, or it isn't centred.
    int leftMargin  = blockX;
    int rightMargin = PANEL_WIDTH - (blockX + blockW);
    int skew        = leftMargin - rightMargin;
    if (skew < 0) skew = -skew;
    checkf(skew <= 1, "%lu: margins are %d left and %d right, skew %d", v,
           leftMargin, rightMargin, skew);
  }
}

// --- The clamp in stepsBlockX must actually hold, for a block too wide to fit.
// It degrades to left-aligned and clips; what it must never do is hand GFX a
// negative cursor, which wraps the label onto another row.
static void test_oversized_block_clamps_instead_of_going_negative() {
  printf("a block too wide for the panel clamps to x=0\n");
  for (int labelW = 0; labelW <= 200; labelW += 7) {
    for (int countW = 0; countW <= 200; countW += 7) {
      int blockX = stepsBlockX(countW, labelW, PANEL_WIDTH);
      checkf(blockX >= 0, "count %dpx + label %dpx gave x=%d", countW, labelW,
             blockX);
      checkf(stepsLabelX(blockX, countW) >= 0,
             "count %dpx + label %dpx put the label at x=%d", countW, labelW,
             stepsLabelX(blockX, countW));
    }
  }
}

// --- Today's worst case should have room to spare. This is the one check here
// that is allowed to be about the CURRENT numbers rather than any valid ones:
// if a future change leaves 99,999 flush against the panel edge, the row is
// one font change away from clipping and somebody should know before it ships.
static void test_worst_case_has_headroom() {
  printf("the widest real count leaves margin at today's numbers\n");
  int blockW = stepsBlockWidth(stepCountWidth(STEPS_MAX), LABEL_W_TOMTHUMB);
  checkf(blockW <= PANEL_WIDTH - 4,
         "\"%lu\" + label is %dpx of %dpx — under 2px of margin a side", STEPS_MAX,
         blockW, PANEL_WIDTH);
}

int main() {
  printf("steps_layout: gap=%d digit=%d sep=%d cap=%lu label(TomThumb)=%d\n\n",
         STEPS_GAP, STEPS_DIGIT_ADVANCE, STEPS_SEP_ADVANCE, STEPS_MAX,
         LABEL_W_TOMTHUMB);

  test_digits_match_printf();
  test_width_matches_the_drawing_loop();
  test_width_is_monotonic();
  test_gap_is_always_exactly_two();
  test_block_stays_on_the_panel_and_is_centred();
  test_oversized_block_clamps_instead_of_going_negative();
  test_worst_case_has_headroom();

  printf("\n%d checks, %d failures\n", checks, failures);
  if (failures == 0) printf("PASS\n");
  else printf("FAIL\n");
  return failures == 0 ? 0 : 1;
}
