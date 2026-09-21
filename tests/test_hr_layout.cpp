// ============================================================================
// Tests for the heart-rate row's geometry (hr_layout.h)
// ============================================================================
//
// Run with `make -C tests`. Same house style as test_steps_layout.cpp: no
// framework, property tests rather than a table of expected pixel columns, and
// no claim to tell you the row LOOKS right — that is still a job for the
// panel. What these pin is the handful of things that can be wrong without
// looking obviously wrong:
//
//   1. The gap between the number and the heart is exactly HR_GAP at every
//      BPM width. Invisible until you compare a 2-digit reading against a
//      3-digit one.
//   2. hrHeartWidth() agrees with the columns drawHeart() actually touches.
//      These are two expressions of one shape; if they drift, the block is
//      off-centre by the difference and nothing says so.
//   3. Both rows share a centre axis. Centring the BPM row was the whole
//      point of the change — if it ever drifts from the steps row's centre by
//      more than integer division costs, the panel stops reading as one
//      column.
//   4. The block stays on the panel, and clamps instead of going negative
//      when it can't.

#include "../hr_layout.h"
#include "../steps_layout.h"

// For PANEL_WIDTH, so widening the panel re-checks this row. config.h is all
// #defines and pulls in no Arduino headers; the Makefile supplies secrets.h
// from the example when a real one isn't present.
#include "../config.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

// The heart's scale in display_ui.cpp. Mirrored rather than shared because it
// lives in that file's anonymous namespace with the rest of its per-pixel
// judgement calls; if it changes there, the sweeps below just exercise a
// different scale, and the scale-independent properties still hold.
static const int HEART_SCALE = 4;

// Widths the BPM can actually take, as GFX reports them for the default
// built-in-at-size-2 style: "--" and two or three digits. NOT what the
// firmware uses — display_ui.cpp measures the string at runtime precisely so a
// font change can't be silently wrong. These are here to give the sweeps a
// realistic range.
static const int NUM_W_MIN = 10;  // one digit, 5px glyph at size 2
static const int NUM_W_MAX = 58;  // wider than any font on this panel renders

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

// --- hrHeartWidth must match the columns drawHeart() inks.
//
// This reimplements that function's extents — and only its extents — so the
// closed form and the drawing are checked against each other. The triangle is
// the widest part: cx - 2*scale .. cx + 2*scale inclusive. The lobes are
// circles of radius `scale` centred a scale either side of cx, so they reach
// cx +/- 2*scale too, and never further.
static void test_heart_width_matches_what_is_drawn() {
  printf("hrHeartWidth matches the columns drawHeart inks\n");
  for (int scale = 1; scale <= 8; scale++) {
    const int cx = 100;  // arbitrary; the extents are relative to it
    int triLeft   = cx - scale * 2;
    int triRight  = cx + scale * 2;
    int lobeLeft  = (cx - scale) - scale;
    int lobeRight = (cx + scale) + scale;
    int left  = triLeft  < lobeLeft  ? triLeft  : lobeLeft;
    int right = triRight > lobeRight ? triRight : lobeRight;

    checkf(hrHeartWidth(scale) == right - left + 1,
           "scale %d: hrHeartWidth says %d, the drawing inks %d columns", scale,
           hrHeartWidth(scale), right - left + 1);
  }
}

// --- THE SPEC: exactly three blank columns between the number's last inked
// column and the heart's first, at every number width.
//
// Asserted against a LITERAL 3, not against HR_GAP, for the reason spelled out
// in test_steps_layout.cpp: checking the layout against the constant it is
// built from passes whatever that constant says. Three pixels is the
// requirement; HR_GAP is one implementation of it. Changing it is a decision,
// so it gets changed here by hand too.
static const int REQUIRED_GAP = 3;

static void test_gap_is_always_exactly_three() {
  printf("the number-to-heart gap is exactly %d at every number width\n",
         REQUIRED_GAP);
  checkf(HR_GAP == REQUIRED_GAP, "HR_GAP is %d, but the row calls for %d",
         HR_GAP, REQUIRED_GAP);

  for (int scale = 2; scale <= 6; scale++) {
    int heartW = hrHeartWidth(scale);
    for (int numW = NUM_W_MIN; numW <= NUM_W_MAX; numW++) {
      int blockX    = hrBlockX(numW, heartW, PANEL_WIDTH);
      int heartLeft = hrHeartCX(blockX, numW, scale) - scale * 2;
      int gap       = heartLeft - (hrNumRightX(blockX, numW) + 1);
      checkf(gap == REQUIRED_GAP,
             "a %dpx number with a scale-%d heart: gap is %dpx, not %d", numW,
             scale, gap, REQUIRED_GAP);
    }
  }
}

// --- The block must fit on the panel, and be centred within the pixel that
// integer division costs.
static void test_block_stays_on_the_panel_and_is_centred() {
  printf("the block fits the panel and is centred to within 1px\n");
  const int heartW = hrHeartWidth(HEART_SCALE);
  // Up to the widest number that still leaves room for the heart; past that
  // the block legitimately clamps, which the clamp test covers.
  for (int numW = NUM_W_MIN; numW <= PANEL_WIDTH - HR_GAP - heartW; numW++) {
    int blockW = hrBlockWidth(numW, heartW);
    int blockX = hrBlockX(numW, heartW, PANEL_WIDTH);

    checkf(blockX >= 0, "%dpx number: block starts at x=%d, off the left edge",
           numW, blockX);
    checkf(blockX + blockW <= PANEL_WIDTH,
           "%dpx number: block is %dpx at x=%d, past the %dpx panel", numW,
           blockW, blockX, PANEL_WIDTH);

    int leftMargin  = blockX;
    int rightMargin = PANEL_WIDTH - (blockX + blockW);
    int skew        = leftMargin - rightMargin;
    if (skew < 0) skew = -skew;
    checkf(skew <= 1, "%dpx number: margins are %d left and %d right, skew %d",
           numW, leftMargin, rightMargin, skew);
  }
}

// --- The two rows must share a centre axis.
//
// This is the property the change was made FOR, and the one no amount of
// staring at either row on its own would catch. Both blocks are centred on the
// same panel, so their midpoints can differ by at most the pixel integer
// division costs each of them.
static void test_both_rows_share_a_centre() {
  printf("the BPM row and the steps row are centred on the same axis\n");
  const int heartW = hrHeartWidth(HEART_SCALE);
  const int labelW = 20;  // "STEPS" in TomThumb, as test_steps_layout.cpp has it

  for (int numW = NUM_W_MIN; numW <= PANEL_WIDTH - HR_GAP - heartW; numW++) {
    int hrX      = hrBlockX(numW, heartW, PANEL_WIDTH);
    int hrMid2   = 2 * hrX + hrBlockWidth(numW, heartW);  // twice the midpoint

    for (unsigned long steps = 0; steps <= STEPS_MAX; steps += 331) {
      int countW  = stepCountWidth(steps);
      int stX     = stepsBlockX(countW, labelW, PANEL_WIDTH);
      int stMid2  = 2 * stX + stepsBlockWidth(countW, labelW);

      int skew = hrMid2 - stMid2;
      if (skew < 0) skew = -skew;
      // Doubled midpoints, so a 1px difference is a skew of 2.
      checkf(skew <= 2,
             "a %dpx number against %lu steps: centres differ by %.1fpx", numW,
             steps, skew / 2.0);
    }
  }
}

// --- The clamp in hrBlockX must hold for a block too wide to fit: it degrades
// to left-aligned and clips on the right, rather than handing GFX a negative
// cursor.
static void test_oversized_block_clamps_instead_of_going_negative() {
  printf("a block too wide for the panel clamps to x=0\n");
  for (int scale = 1; scale <= 12; scale++) {
    int heartW = hrHeartWidth(scale);
    for (int numW = 0; numW <= 200; numW += 7) {
      int blockX = hrBlockX(numW, heartW, PANEL_WIDTH);
      checkf(blockX >= 0, "%dpx number + scale-%d heart gave x=%d", numW, scale,
             blockX);
      checkf(hrNumRightX(blockX, numW) >= -1,
             "%dpx number + scale-%d heart put the number's right edge at %d",
             numW, scale, hrNumRightX(blockX, numW));
      checkf(hrHeartCX(blockX, numW, scale) - scale * 2 >= 0,
             "%dpx number + scale-%d heart put the heart's left at %d", numW,
             scale, hrHeartCX(blockX, numW, scale) - scale * 2);
    }
  }
}

// --- Today's worst case should have room to spare. The one check here that is
// about the CURRENT numbers rather than any valid ones: a three-digit BPM in
// the default style is the widest thing this row ever shows, and if it ever
// sits flush against the panel edge the row is one font change from clipping.
static void test_worst_case_has_headroom() {
  printf("the widest real BPM leaves margin at today's numbers\n");
  // "199" in the built-in font at size 2: three 5px glyphs, 12px apart.
  const int WIDEST_REAL_NUM_W = 3 * 12 - 2;
  int blockW = hrBlockWidth(WIDEST_REAL_NUM_W, hrHeartWidth(HEART_SCALE));
  checkf(blockW <= PANEL_WIDTH - 4,
         "a 3-digit BPM + heart is %dpx of %dpx — under 2px of margin a side",
         blockW, PANEL_WIDTH);
}

int main() {
  printf("hr_layout: gap=%d heart(scale %d)=%dpx panel=%d\n\n", HR_GAP,
         HEART_SCALE, hrHeartWidth(HEART_SCALE), PANEL_WIDTH);

  test_heart_width_matches_what_is_drawn();
  test_gap_is_always_exactly_three();
  test_block_stays_on_the_panel_and_is_centred();
  test_both_rows_share_a_centre();
  test_oversized_block_clamps_instead_of_going_negative();
  test_worst_case_has_headroom();

  printf("\n%d checks, %d failures\n", checks, failures);
  if (failures == 0) printf("PASS\n");
  else printf("FAIL\n");
  return failures == 0 ? 0 : 1;
}
