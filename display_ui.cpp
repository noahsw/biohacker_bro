#include "display_ui.h"
#include "config.h"
#include "hr_zones.h"
#include "steps_layout.h"   // the bottom row's geometry, shared with tests/

#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>  // BPM digits: 17px cap height, uniform 13px advance
#include <Fonts/TomThumb.h>        // "STEPS": 3x5, the smallest legible capitals

MatrixPanel_I2S_DMA *display = nullptr;

// ============================================================================
// LAYOUT (64x32)
// ============================================================================
//   y0                the fixed 5-segment zone legend, lit at all times
//   y1..2             the 2px-tall live HR bar (current zone's color)
//   x?..36,  y6..19   BPM, built-in 5x7 at size 2, right-aligned, zone-colored
//   x40..56, y6..20   heart, pulsing in brightness on each beat (tops flush
//                     with the digits; the point hangs one row below)
//   y24..30           step count, built-in 5x7, 2px ticks, and
//   y26..30           "STEPS" in TomThumb 3x5 and dim, the two measured
//                     together 2px apart and centred on the 64px width
//
// Vertical budget is roughly 10 / 60 / 30: three rows of gauge, nineteen of
// heart rate, ten of steps. The gauge is on TOP, touching the heart rate and
// nothing else — the bar IS the heart rate, and adjacency is the only thing
// on a panel this size that says which number a gauge belongs to. Sandwiched
// between the two numbers it would touch both and mean neither.
//
// The heart-rate row is two columns: the number right-aligned to x36 with the
// heart beside it. Right-aligning is what keeps the BPM still as it gains or
// loses a digit — the ones column never moves. The steps row is one centred
// block instead: count and label together, so the pair stays balanced.
//
// The BPM is unlabelled and the steps are labelled, which is deliberate: the
// heart beside the number says "heart rate" better than three letters would,
// and there is no room for both a heart and a "BPM". A bare number under a
// heart rate, though, reads as another cardiac figure, so that one gets a word.
//
// FONT NOTE. FreeSans12pt7b, not the built-in font at size 2 or 3. The built-in
// font only scales by whole numbers: 14px tall (what this used to be) or 21px,
// nothing between, and at 21px three digits are 54px wide, leaving no room for
// a heart. FreeSans12pt7b is 17px tall in 39px. It's also the only 17px sans
// here whose digits all share one 13px advance — FreeSansBold's '1' is a pixel
// WIDER than its other digits, which would shuffle the number sideways every
// time the hundreds digit appeared. Regular rather than bold was an aesthetic
// call (instrument, not signage); the strokes differ by about one pixel, so
// swapping to FreeSansBold12pt7b is a one-line change if it reads too thin on
// the panel — but it needs the heart dropped to scale 4 to fit.
namespace {

const int BAR_TOP_Y    = 1;   // bar occupies rows 1 and 2
const int LEGEND_Y     = 0;

// Both numbers share this right edge, in the left-hand column.
//
// Everything except the gauge sits 5px left of where the width budget alone
// would put it. Packed hard against the right edge the whole block looked
// shunted over, because the numbers are right-aligned: the left margin grows
// as the BPM drops to two digits, so the content is at its most lopsided
// exactly when you're resting and looking at it. The gauge spans the full
// width and is unaffected.
const int NUM_RIGHT_X  = 36;
// FreeSans12pt7b is baseline-positioned; 22 - 17 (cap height) = top row 5.
// Baseline 20, not 19: FreeMonoBold12pt7b's digits have a -14 yOffset and are
// 15 tall, so this puts them on rows 6..20 — exactly the rows a scale-4 heart
// at cy=12 occupies. Number and heart are flush top and bottom, no fudge.
//
// The whole heart-rate row (number, heart, seven-seg) sits one row lower than
// it first did: with the gauge on rows 0..2 it was crowding the bar, and the
// spare row was below it, not above.
const int BPM_BASELINE_Y = 20;
// Built-in font is top-positioned, and 14px tall at size 2: rows 6..19.
const int BPM_BUILTIN_TOP_Y = 6;
// Seven-segment is drawn from its top row: rows 4..19, one taller than
// FreeMonoBold's 15. Its two bowls are identical rectangles by construction,
// so an 8 cannot come out lopsided the way a rasterised one can.
const int SEVENSEG_TOP_Y = 4;
const int SEVENSEG_W = 9, SEVENSEG_H = 16, SEVENSEG_T = 2, SEVENSEG_GAP = 2;
// Built-in font is top-positioned.
const int STEPS_TOP_Y  = 24;
// TomThumb is baseline-positioned; 31 - 5 = top row 26.
const int STEPS_LABEL_BASELINE_Y = 31;
// Steps are green, and specifically not blue. Blue is Z2's color on both the
// legend and the bar, so a blue step count read as a heart-rate element that
// had wandered to the bottom of the panel — the only two numbers up there are
// already easy to confuse, and sharing a hue with a zone made it worse. Green
// appears nowhere in the zone ramp, so it can't be misread as a zone, and it
// stays clear of Z3's yellow.
//
// The label shares the count's color; its smaller 3x5 face is what keeps it
// from out-shouting the number.
const uint16_t STEPS_COLOR_RGB[3]       = {0, 210, 80};
const uint16_t STEPS_LABEL_COLOR_RGB[3] = {STEPS_COLOR_RGB[0], STEPS_COLOR_RGB[1],
                                           STEPS_COLOR_RGB[2]};
// STEPS_GAP, the blank columns between the count and the word, lives in
// steps_layout.h with the rest of that row's arithmetic. It is 2 rather than 1
// because at 3x5 a single column crowded the label into the number badly
// enough that the two read as one token.
// The heart, to the right of the BPM.
const int HEART_CX     = 48;
const int HEART_CY     = 12;
const int HEART_SCALE  = 4;
const float HEART_FLOOR = 0.35f;  // brightness between beats; see drawMainScreen

unsigned long lastBeatTime = 0;

// The zone ramp, as it appears on the legend and the bar. Ordered so
// PERCEIVED brightness climbs the whole way up — dim neutral, bright neutral,
// then a cold-to-hot hue ramp — so the bar reads as intensity growing even
// before you've learned which color means which zone.
//
// White is capped at 140 rather than 255 on purpose. At full white, Z1 was
// the brightest thing on the panel and the step up to blue read as a DROP in
// intensity, which is backwards. Holding white below blue's apparent
// brightness keeps the ramp monotonic.
uint16_t zonePalette(int zone) {
  switch (zone) {
    case 0:  return display->color565(45, 45, 45);    // gray   — barely moving
    case 1:  return display->color565(140, 140, 140); // white  — warming up
    case 2:  return display->color565(0, 110, 255);   // blue   — moving
    case 3:  return display->color565(255, 190, 0);   // yellow — going for it
    default: return display->color565(255, 30, 30);   // red    — Z4/5
  }
}

// The same zones for the BPM digits, held at a uniform brightness so the
// number is equally readable in every zone — only the hue changes.
//
// Consequence worth knowing: at equal brightness, "gray" and "white" ARE the
// same color, so Z0 and Z1 can't be told apart in the number and both render
// white. That's the right trade — the number's job is to be readable at rest,
// and the bar below already says which of the two you're in. The blue is
// lifted off its pure primary to buy legibility: a pure (0,110,255) numeral is
// noticeably harder to read than a yellow one at the same nominal value,
// because blue LEDs carry the least perceived brightness.
//
// Z4's red is NOT lifted, though it was. (255,70,70) read as pink on the panel
// and plainly failed to match the red bar above it — which is the one thing
// the top zone's color has to do, since bar and number are 4px apart and the
// eye compares them directly. Red LEDs are bright enough that the lift bought
// very little here anyway, and a same-hue-different-color pair looks like a
// bug, not like emphasis. Any change to this value should be checked against
// zonePalette's red side by side, not judged on its own.
uint16_t zoneTextPalette(int zone) {
  switch (zone) {
    case 0:  return display->color565(255, 255, 255); // white
    case 1:  return display->color565(255, 255, 255); // white (see above)
    case 2:  return display->color565(80, 165, 255);  // blue, lifted
    case 3:  return display->color565(255, 205, 0);   // yellow
    default: return display->color565(255, 30, 30);   // red, matching the bar
  }
}

// Simple pixel-art heart: two circles for the top lobes + a triangle for the
// point. Vertical extent is [cy - scale - scale/2, cy + scale*2], i.e. 15 rows
// at scale 4, one more than size-2 text — fillCircle spans 2r+1 rows, not 2r.
//
// That odd row means the point hangs one row below the BPM digits' baseline.
// Tried trimming it to cy + scale*2 - 1 so the two blocks were flush, and
// rejected it on looks: at this scale the tip is only a couple of pixels wide,
// so removing a row blunts it enough that the silhouette stops reading as a
// heart. A clean bounding box isn't worth a heart that looks wrong, on a
// costume whose whole job is to be recognised at a glance. Keep the point.
void drawHeart(int cx, int cy, int scale, uint16_t color) {
  display->fillCircle(cx - scale, cy - scale / 2, scale, color);
  display->fillCircle(cx + scale, cy - scale / 2, scale, color);
  display->fillTriangle(cx - scale * 2, cy,
                         cx + scale * 2, cy,
                         cx, cy + scale * 2,
                         color);
}

// Draws a step count with two-pixel diagonal ticks in place of commas.
//
// The font's own comma is 6px of advance — as wide as a whole digit — which
// on a 42px column is an absurd price for a separator. These ticks cost 3px:
// two pixels on a diagonal at the baseline, which is enough to group the
// digits without pretending to be punctuation at this size.
//
// Drawn digit by digit rather than with print() because GFX has no way to
// vary advance mid-string; the separator has to be positioned by hand.
void drawStepCount(unsigned long value, int rightEdge, int topY, uint16_t color) {
  char digits[12];
  int n = snprintf(digits, sizeof(digits), "%lu", value);
  int width = stepCountWidth(value);

  display->setFont(NULL);
  display->setTextSize(1);

  int x = rightEdge - width + 1;
  for (int i = 0; i < n; i++) {
    if (i > 0 && (n - i) % 3 == 0) {
      // Bottom-left leaning, so it reads as falling away from the digit
      // before it rather than as a stray dot between two numbers.
      display->drawPixel(x + 1, topY + 5, color);
      display->drawPixel(x,     topY + 6, color);
      x += STEPS_SEP_ADVANCE;
    }
    // bg == color puts drawChar in transparent mode (GFX only fills a
    // background when the two differ), so the ticks aren't painted over.
    display->drawChar(x, topY, digits[i], color, color, 1);
    x += STEPS_DIGIT_ADVANCE;
  }
}

// Draws text so its right edge lands on `rightEdge`, for whatever font is
// currently set. Asks GFX for the rendered bounds rather than assuming a
// character width: the built-in font is a fixed 6px per char, but a free font
// is proportional and carries a left side bearing, so computing this by hand
// gets it wrong by a pixel or two per string.
//
// `y` means different things per font, which is GFX's design, not ours: for
// the built-in font it's the TOP row of the glyphs; for a free font it's the
// BASELINE, with the glyphs sitting above it. Callers below say which.
void printRightAligned(const char *text, int rightEdge, int y) {
  int16_t bx, by;
  uint16_t bw, bh;
  display->getTextBounds(text, 0, y, &bx, &by, &bw, &bh);
  display->setCursor(rightEdge - bw - bx + 1, y);
  display->print(text);
}

// --- Seven-segment digits -------------------------------------------------
//
// A scoreboard/instrument numeral, drawn from seven rectangles rather than
// loaded from a font. Worth it here for three reasons a bitmap font can't
// match: the stroke thickness is a parameter (no font ships at "a bit
// bolder"), every digit is exactly the same width, and it costs no font data
// at all — the geometry IS the glyph.
//
// Segment layout and bit assignment:
//        aaaa          a=1   b=2   c=4   d=8
//       f    b         e=16  f=32  g=64
//       f    b
//        gggg
//       e    c
//       e    c
//        dddd
const uint8_t SEG_DIGIT[10] = {
  0x3F, // 0: abcdef
  0x06, // 1: bc
  0x5B, // 2: abdeg
  0x4F, // 3: abcdg
  0x66, // 4: bcfg
  0x6D, // 5: acdfg
  0x7D, // 6: acdefg
  0x07, // 7: abc
  0x7F, // 8: all
  0x6F, // 9: abcdfg
};

// Draws one digit with its top-left at (x, y). Horizontal segments span the
// full width; verticals are half-height plus one stroke so they meet the
// middle bar cleanly instead of leaving a notch at the joint.
void drawSevenSegDigit(int d, int x, int y, int w, int h, int t, uint16_t on,
                       uint16_t off) {
  uint8_t m = (d >= 0 && d <= 9) ? SEG_DIGIT[d] : 0;
  int midY = y + (h - t) / 2;
  int vH   = (h + t) / 2;
  int rx   = x + w - t;
  int botY = y + h - t;
  // Each segment is drawn in `on` or `off`; `off` is normally the background,
  // but passing a dim colour gives the unlit-segment ghosting of a real LED
  // display. Left as a parameter rather than hardcoded so it stays a choice.
  display->fillRect(x,   y,    w, t,  (m & 0x01) ? on : off); // a
  display->fillRect(rx,  y,    t, vH, (m & 0x02) ? on : off); // b
  display->fillRect(rx,  midY, t, vH, (m & 0x04) ? on : off); // c
  display->fillRect(x,   botY, w, t,  (m & 0x08) ? on : off); // d
  display->fillRect(x,   midY, t, vH, (m & 0x10) ? on : off); // e
  display->fillRect(x,   y,    t, vH, (m & 0x20) ? on : off); // f
  display->fillRect(x,   midY, w, t,  (m & 0x40) ? on : off); // g
}

// How "contracted" the heart is right now, 0.0 (relaxed) to 1.0 (full thump).
//
// This drives BRIGHTNESS, not size. An earlier version scaled the geometry,
// but the heart is only ~16px across, so the smallest size step available is
// a 25% jump in width — it popped between two shapes instead of beating. A
// brightness envelope has 256 steps to work with, so the same envelope reads
// as a smooth pulse. The heart never goes fully dark (see the 0.35 floor in
// drawMainScreen): it glows and surges rather than blinking.
//
// Shape is lub-dub: a sharp contraction decaying over ~150ms, then a smaller
// second beat around 280ms, then quiet until the next one. At 60bpm you see a
// distinct double-thump with a rest; by 170bpm they merge into a flutter,
// which is the honest thing for it to do.
float expDecay(float tMs, float tauMs) {
  if (tMs < 0.0f) return 0.0f;
  return expf(-tMs / tauMs);
}

// Decay constants are deliberately slower than a real heart's mechanics.
//
// At 150ms/120ms the envelope was physiologically closer, but at a RESTING
// 55bpm that's a 150ms flash every 1.1 seconds -- measured on hardware as a
// clean 0.00..1.00 swing at 62fps, so the code was right and it still read as
// a flicker rather than a heartbeat. The eye wants a rise and fall it can
// follow, not a strobe.
//
// The original envelope, restored after three attempts at "improving" it all
// came out worse on the panel. Lub-dub: a sharp contraction decaying over
// ~150ms, a smaller second beat at 280ms, then quiet. At 60bpm you see a
// distinct double-thump with a rest; by 170bpm they merge into a flutter.
//
// THE TRAP, which cost three rounds: an earlier version squared this envelope
// to deepen the apparent pulse. Squaring an exponential HALVES its time
// constant -- exp(-t/260)^2 is exp(-t/130) -- so every attempt to lengthen
// the decay was silently cancelled and then some, and the pulse kept coming
// out snappier the longer the tau was set. If you touch these constants,
// check what the brightness mapping in drawMainScreen does to them first.
float beatIntensity() {
  float t = (float)(millis() - lastBeatTime);
  float i = expDecay(t, 150.0f) + 0.45f * expDecay(t - 280.0f, 120.0f);
  if (i > 1.0f) i = 1.0f;
  return i;
}

} // namespace

// The built-in 5x7 at size 2, chosen on hardware over four real typefaces.
//
// Which is where this started, and worth recording so nobody re-runs the
// search: FreeSans read as too curvy, FreeSansBold likewise and its '1' is a
// pixel wider than its other digits, seven-segment as too blocky, and
// FreeMonoBold18pt is 63px for three digits — the whole panel, nowhere to put
// the heart. The useful finding is that NOTHING between 14px and 21px both
// fits beside a heart and looks right at 2.5mm pitch, so the blocky face
// isn't a compromise, it's the only thing in the range.
//
// It also matches the step count below it, which the free fonts never did.
BpmStyle bpmStyle = BPM_BUILTIN_2;

void setBpmStyle(BpmStyle style) { bpmStyle = style; }

const char *bpmStyleName(BpmStyle style) {
  switch (style) {
    case BPM_BUILTIN_2: return "builtin-x2";
    case BPM_SANS:      return "FreeSans12pt";
    case BPM_SANS_BOLD: return "FreeSansBold12pt";
    case BPM_MONO_BOLD: return "FreeMonoBold12pt";
    case BPM_SEVEN_SEG: return "seven-segment";
    default:            return "?";
  }
}

void drawSolidTest(uint8_t r, uint8_t g, uint8_t b) {
  display->fillScreen(display->color565(r, g, b));
  display->flipDMABuffer();
}

float heartBeatLevel() { return beatIntensity(); }

uint16_t zoneColor(int bpm) {
  return zoneTextPalette(hrZone(bpm));
}

bool displaySetup() {
  HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN);
  // Two fixes for visible flicker, which had two separate causes:
  //  - Tearing: we clear and redraw the whole frame every loop, and with a
  //    single buffer the panel was scanning out half-drawn frames. Double
  //    buffering means the panel only ever shows a finished frame; we draw
  //    into the back one and flip at the end of drawMainScreen().
  //  - Refresh: the library defaults to an 8MHz pixel clock and a 60Hz
  //    minimum, which is low enough to beat visibly against both eyes and
  //    camera shutters. 16MHz gives enough headroom for 120Hz at full colour
  //    depth. If this ever shows ghosting or colour fringing on the panel,
  //    back HUB75_CLOCK_HZ down to HZ_8M first, then drop the refresh rate.
  mxconfig.double_buff = true;
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_16M;
  mxconfig.min_refresh_rate = 120;
  // This panel clocks data in on the NEGATIVE edge. With the library's default
  // (positive), everything landed one pixel to the left, so the last column
  // showed the next row's first pixel instead of its own — visible as faint
  // unidentifiable dots at x=63 where the red Z4 legend slice should have
  // been. Everything else lined up, because a 1px shift is invisible until it
  // wraps at the panel edge.
  mxconfig.clkphase = false;
  mxconfig.gpio.r1 = R1_PIN;
  mxconfig.gpio.g1 = G1_PIN;
  mxconfig.gpio.b1 = B1_PIN;
  mxconfig.gpio.r2 = R2_PIN;
  mxconfig.gpio.g2 = G2_PIN;
  mxconfig.gpio.b2 = B2_PIN;
  mxconfig.gpio.a = A_PIN;
  mxconfig.gpio.b = B_PIN;
  mxconfig.gpio.c = C_PIN;
  mxconfig.gpio.d = D_PIN;
  mxconfig.gpio.e = E_PIN;
  mxconfig.gpio.lat = LAT_PIN;
  mxconfig.gpio.oe = OE_PIN;
  mxconfig.gpio.clk = CLK_PIN;

  display = new MatrixPanel_I2S_DMA(mxconfig);
  bool ok = display->begin();
  display->setBrightness8(90); // 0-255, tune for how bright you want it all night
  // The panel is 64px wide = 10 chars at size 1, 5 chars at size 2. Adafruit
  // GFX wraps overflowing text onto a second line by default, which on a 32px
  // panel means a stray letter alone on the row below. Truncate instead; the
  // layouts below are sized to fit, and this keeps a mistake from looking
  // like a rendering bug.
  display->setTextWrap(false);
  display->clearScreen();
  return ok;
}

void updateHeartbeatPhase(int bpm) {
  if (bpm <= 0) return;
  unsigned long beatIntervalMs = 60000UL / bpm;
  unsigned long now = millis();
  if (now - lastBeatTime >= beatIntervalMs) {
    // Advance by exactly one interval rather than snapping to now. The loop
    // runs on a 15ms tick, so `lastBeatTime = now` rounded every beat up to
    // 15ms late and the error accumulated -- the heart ran perhaps 1.5% slow
    // forever. Small, but it's free to be exact.
    lastBeatTime += beatIntervalMs;
    // Resync if we've fallen more than a whole beat behind, which happens
    // when the BPM jumps or the loop stalls. Without this the heart would
    // machine-gun through the backlog catching up.
    if (now - lastBeatTime > beatIntervalMs) lastBeatTime = now;
  }
}

void drawMainScreen(int bpm, bool connected, unsigned long steps) {
  display->clearScreen();

  char buf[16];  // "99,999" plus slack

  // --- Zone legend: the fixed scale on the very top row, lit whether or not
  // we have a reading, so the bar below it always has context.
  for (int z = 0; z < 5; z++) {
    int x0 = zoneSliceX(z, PANEL_WIDTH);
    int x1 = zoneSliceX(z + 1, PANEL_WIDTH);
    display->fillRect(x0, LEGEND_Y, x1 - x0, 1, zonePalette(z));
  }

  // --- The live bar, 2px tall, hanging off the legend.
  //
  // Painted in the LEGEND's colors slice by slice rather than one flat color:
  // filling to the middle of the blue zone gives a run of gray, then white,
  // then blue. The bar is literally the legend lit up to where you are — it
  // thickens from 1px to 3px behind you — so the zone is read from where the
  // fill STOPS, not from what color it is.
  if (connected) {
    int width = (int)(barFraction(bpm) * PANEL_WIDTH + 0.5f);
    if (width < 1) width = 1; // always show something so it never reads as "off"
    for (int z = 0; z < 5; z++) {
      int x0 = zoneSliceX(z, PANEL_WIDTH);
      if (width <= x0) break;              // fill ended before this slice
      int x1 = zoneSliceX(z + 1, PANEL_WIDTH);
      if (width < x1) x1 = width;          // partial slice: this is the tip
      display->fillRect(x0, BAR_TOP_Y, x1 - x0, 2, zonePalette(z));
    }
  }

  // --- Heart: always red (it's a heart), pulsing in brightness on each beat.
  // Dimmed to a dark ember while we're still hunting for the strap, so a
  // stale reading can never look live.
  //
  // The beat lives here rather than on the digits or the bar. Tried it on the
  // digits and it reads as a failing display, not a pulse — the heart SHAPE is
  // what makes a brightness envelope legible as a heartbeat. The bar would
  // work (it's a shape, and it is the heart rate), and it's a two-line change
  // if you ever want it, but with a heart on the panel two pulsing things
  // would compete.
  // Linear, with a 0.35 floor: the original, restored. A squared curve with a
  // lower floor was tried to make the pulse deeper and read as jerky -- see
  // the note on beatIntensity(), which that squaring was also secretly
  // halving. The heart glows and surges rather than blinking.
  uint16_t heartColor;
  if (connected) {
    float level = HEART_FLOOR + (1.0f - HEART_FLOOR) * beatIntensity();
    heartColor = display->color565((int)(255 * level), (int)(20 * level), (int)(20 * level));
  } else {
    heartColor = display->color565(50, 0, 0);
  }
  // scale 4 spans [cy-6, cy+8] = 15 rows, so cy=12 puts it on rows 6..20 and
  // x40..56: the same 15-row height as the digits. Dropped from scale 5 to make that margin fit; the number and
  // the heart being the same height is worth more than 3px of heart.
  //
  // NOT bounding-box aligned with the digits (rows 5..21), on purpose. The
  // heart is two fat lobes tapering to a point, so its area sits high in its
  // own box: with the boxes aligned it read as floating above the number.
  // Integrating the two discs against the triangle puts the heart's centre of
  // area at about row 11, matching the digits' centre — so this is aligned by
  // MASS, which is what the eye measures, and the boxes sitting a row apart
  // is the price.
  drawHeart(HEART_CX, HEART_CY, HEART_SCALE, heartColor);

  // --- BPM, in the current zone's hue at a constant brightness.
  // Free font: the y argument is the BASELINE. Cap height is 17, so a baseline
  // of 22 puts the digits on rows 5..21.
  uint16_t bpmColor = connected ? zoneColor(bpm) : display->color565(70, 70, 70);
  if (connected) {
    snprintf(buf, sizeof(buf), "%d", bpm);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }

  if (bpmStyle == BPM_BUILTIN_2) {
    // Built-in font: y is the TOP row, not the baseline, so 6 puts the 14px
    // digits on rows 6..19 against the heart's 6..20 — tops flush, the
    // heart's point hanging one row below, which is the same relationship the
    // heart has always had to the digits here.
    display->setFont(NULL);
    display->setTextSize(2);
    display->setTextColor(bpmColor);
    printRightAligned(buf, NUM_RIGHT_X, BPM_BUILTIN_TOP_Y);
    display->setTextSize(1);
  } else if (bpmStyle == BPM_SEVEN_SEG) {
    // Laid out by hand rather than through GFX: 11px cells with a 2px gap,
    // right-aligned on the same x36 edge as the other BPM styles. "--" while
    // disconnected becomes middle bars only, which is what a real instrument
    // with no signal shows.
    const int CW = SEVENSEG_W, CH = SEVENSEG_H, CT = SEVENSEG_T;
    const int GAP = SEVENSEG_GAP, TOP = SEVENSEG_TOP_Y;
    int n = (int)strlen(buf);
    int totalW = n * CW + (n - 1) * GAP;
    int x = NUM_RIGHT_X - totalW + 1;
    for (int i = 0; i < n; i++) {
      int cx = x + i * (CW + GAP);
      if (buf[i] == '-') {
        display->fillRect(cx, TOP + (CH - CT) / 2, CW, CT, bpmColor);
      } else {
        drawSevenSegDigit(buf[i] - '0', cx, TOP, CW, CH, CT, bpmColor, 0);
      }
    }
  } else {
    switch (bpmStyle) {
      case BPM_SANS_BOLD: display->setFont(&FreeSansBold12pt7b); break;
      case BPM_MONO_BOLD: display->setFont(&FreeMonoBold12pt7b); break;
      default:            display->setFont(&FreeSans12pt7b);     break;
    }
    display->setTextSize(1);
    display->setTextColor(bpmColor);
    printRightAligned(buf, NUM_RIGHT_X, BPM_BASELINE_Y);
  }

  // --- Step count and "STEPS", centred in the panel as ONE block.
  //
  // The count is not right-aligned to the BPM's edge: the pair is measured
  // together, always STEPS_GAP apart, and the whole thing is centred on the
  // 64px width. So the number-plus-word reads as a single unit that stays
  // balanced under the heart-rate row, and a digit gained or lost moves both
  // halves outward by half a pixel's worth rather than shoving the label
  // sideways. The cost is that the count's ones column drifts as the number
  // grows — acceptable here, because the steps only ever climb and nobody
  // watches that digit the way they watch a BPM.
  //
  // The label is 3x5 rather than the 5x7 used everywhere else: at 5x7 the word
  // is 30px and leaves no room for the count beside it, and it's a label,
  // which should never out-shout its number. Dim for the same reason.
  //
  // Labelled while the BPM isn't — see the layout note above.
  unsigned long shownSteps = steps > STEPS_MAX ? STEPS_MAX : steps;
  int countW = stepCountWidth(shownSteps);

  // Ask GFX for the label's rendered width rather than assuming 4px an advance;
  // bx is its left side bearing, which has to come back out when positioning.
  display->setFont(&TomThumb);
  int16_t lbx, lby;
  uint16_t lbw, lbh;
  display->getTextBounds("STEPS", 0, STEPS_LABEL_BASELINE_Y, &lbx, &lby, &lbw,
                         &lbh);

  int blockX = stepsBlockX(countW, (int)lbw, PANEL_WIDTH);

  drawStepCount(shownSteps, stepCountRightX(blockX, countW), STEPS_TOP_Y,
                display->color565(STEPS_COLOR_RGB[0], STEPS_COLOR_RGB[1],
                                  STEPS_COLOR_RGB[2]));

  display->setFont(&TomThumb);
  display->setTextColor(display->color565(STEPS_LABEL_COLOR_RGB[0],
                                          STEPS_LABEL_COLOR_RGB[1],
                                          STEPS_LABEL_COLOR_RGB[2]));
  display->setCursor(stepsLabelX(blockX, countW) - lbx, STEPS_LABEL_BASELINE_Y);
  display->print("STEPS");

  // Leave the font as we found it, so anything drawn later (or by a test
  // sketch) isn't silently rendered in TomThumb.
  display->setFont(NULL);

  // Frame complete — show it. With double_buff on, nothing drawn above has
  // been visible until this call.
  display->flipDMABuffer();
}

void drawStepsScreen(unsigned long steps) {
  display->clearScreen();
  uint16_t color = display->color565(STEPS_COLOR_RGB[0], STEPS_COLOR_RGB[1],
                                     STEPS_COLOR_RGB[2]);
  display->setTextColor(color);
  display->setTextSize(1);
  display->setCursor(2, 4);
  display->print("STEPS");
  display->setTextSize(2);
  display->setCursor(2, 16);
  display->print(steps);
  display->flipDMABuffer();
}
