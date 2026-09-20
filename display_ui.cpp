#include "display_ui.h"
#include "config.h"

MatrixPanel_I2S_DMA *display = nullptr;

// ============================================================================
// LAYOUT (64x32)
// ============================================================================
//   x0..21,  y0..28   heart, beating in time with the real BPM
//   x23..63, y3..17   BPM, size 2, in the current zone's color
//   x23..63, y21..28  step count, size 1, with a little footprint glyph
//   y29..30           the 2px-tall live HR bar (current zone's color)
//   y31               the fixed 5-segment zone legend, lit at all times
// The bar sits directly on top of the legend on purpose: the legend is the
// scale and the bar is the needle, so any gap between them would make the
// two read as unrelated widgets.
namespace {

const int BAR_TOP_Y    = 29;  // bar occupies rows 29 and 30
const int LEGEND_Y     = 31;

unsigned long lastBeatTime = 0;

// Lower bound of each zone, plus the ceiling of the top zone. Indexed by
// zone number, so zoneFloor[z]..zoneFloor[z+1] is zone z's BPM range.
const int zoneFloor[6] = { 0, ZONE1_BPM, ZONE2_BPM, ZONE3_BPM, ZONE4_BPM, ZONE_MAX_BPM };

uint16_t zonePalette(int zone) {
  switch (zone) {
    case 0:  return display->color565(255, 255, 255); // white
    case 1:  return display->color565(110, 110, 110); // gray
    case 2:  return display->color565(0, 110, 255);   // blue
    case 3:  return display->color565(0, 220, 60);    // green
    default: return display->color565(255, 30, 30);   // red (Z4/5)
  }
}

// Left edge of zone `z`'s 20% slice. Derived from the panel width rather than
// hardcoded so the 64px doesn't quietly round away: 64/5 = 12.8px per zone,
// so the slices come out 12 or 13 wide and still tile the row exactly.
int zoneSliceX(int z) {
  return (PANEL_WIDTH * z) / 5;
}

// Simple pixel-art heart, drawn centered, scaled slightly for the "beat"
void drawHeart(int cx, int cy, int scale, uint16_t color) {
  // Two circles for the top lobes + a triangle for the bottom point
  display->fillCircle(cx - scale, cy - scale / 2, scale, color);
  display->fillCircle(cx + scale, cy - scale / 2, scale, color);
  display->fillTriangle(cx - scale * 2, cy,
                         cx + scale * 2, cy,
                         cx, cy + scale * 2,
                         color);
}

// Two tiny footprints, to label the step count without spending 5 characters
// of an already-narrow row on the word "STEPS".
void drawFootprints(int x, int y, uint16_t color) {
  display->fillRect(x,     y,     2, 3, color); // left foot: sole
  display->drawPixel(x,     y + 4,    color);   //            toes
  display->fillRect(x + 3, y + 2, 2, 3, color); // right foot, offset lower
  display->drawPixel(x + 3, y + 6,    color);
}

// Draws text ending at `rightEdge` instead of starting at a cursor, so a
// number stays inside the panel as it gains digits. 6px per char at size 1,
// scaling linearly with text size.
void printRightAligned(const char *text, int rightEdge, int y, int textSize) {
  int width = strlen(text) * 6 * textSize;
  display->setTextSize(textSize);
  display->setCursor(rightEdge - width + 1, y);
  display->print(text);
}

// How "expanded" the heart is right now, 0.0 (relaxed) to 1.0 (full thump).
// Modeled as lub-dub rather than a square wave: a sharp contraction that
// decays over ~180ms, then a smaller second beat, then stillness until the
// next one. At 60bpm you see a distinct double-thump with a rest; at 170bpm
// the beats run together into a fast flutter, which is the point.
float beatIntensity() {
  unsigned long sinceBeat = millis() - lastBeatTime;
  if (sinceBeat < 180) return 1.0f - (sinceBeat / 180.0f);          // lub
  if (sinceBeat < 300) return 0.5f - ((sinceBeat - 180) / 240.0f);  // dub
  return 0.0f;
}

// Fraction (0..1) of the way along the FULL bar for this BPM: each zone owns
// exactly 20% of the width, and position inside that 20% is progress through
// the zone's own BPM range. So the bar is piecewise-linear, not linear in BPM
// — which is what makes "am I nearly into the next zone?" readable at a glance.
float barFraction(int bpm) {
  int z = hrZone(bpm);
  int lo = zoneFloor[z];
  int hi = zoneFloor[z + 1];
  float within = (float)(bpm - lo) / (float)(hi - lo);
  if (within < 0.0f) within = 0.0f;
  if (within > 1.0f) within = 1.0f; // above ZONE_MAX_BPM: pin the bar full
  return (z + within) / 5.0f;
}

} // namespace

int hrZone(int bpm) {
  if (bpm < ZONE1_BPM) return 0;
  if (bpm < ZONE2_BPM) return 1;
  if (bpm < ZONE3_BPM) return 2;
  if (bpm < ZONE4_BPM) return 3;
  return 4;
}

uint16_t zoneColor(int bpm) {
  return zonePalette(hrZone(bpm));
}

bool displaySetup() {
  HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN);
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
    lastBeatTime = now;
  }
}

void drawMainScreen(int bpm, bool connected, unsigned long steps) {
  display->clearScreen();

  uint16_t zColor = connected ? zoneColor(bpm) : display->color565(60, 60, 60);

  // --- Heart: always red (it's a heart), but dimmed while we're still
  // hunting for the strap so a stale reading can't look live.
  uint16_t heartColor = connected ? display->color565(255, 20, 20)
                                  : display->color565(50, 0, 0);
  int scale = connected ? 3 + (int)(beatIntensity() + 0.5f) : 3;
  drawHeart(11, 12, scale, heartColor);

  // --- BPM, in the current zone's color
  char buf[12];
  if (connected) {
    snprintf(buf, sizeof(buf), "%d", bpm);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  display->setTextColor(zColor);
  printRightAligned(buf, 63, 3, 2);

  // --- Steps
  uint16_t stepColor = display->color565(0, 180, 255);
  display->setTextColor(stepColor);
  snprintf(buf, sizeof(buf), "%lu", steps);
  // The glyph lives in x24..29 and the number is right-aligned to 63, so it
  // only fits alongside up to 5 digits. Past 99,999 steps, drop the glyph
  // rather than let the number collide with it.
  if (strlen(buf) <= 5) drawFootprints(24, 21, stepColor);
  printRightAligned(buf, 63, 21, 1);

  // --- Zone legend: the fixed scale along the very bottom row, lit whether
  // or not we have a reading, so the bar above it always has context.
  for (int z = 0; z < 5; z++) {
    int x0 = zoneSliceX(z);
    int x1 = zoneSliceX(z + 1);
    display->fillRect(x0, LEGEND_Y, x1 - x0, 1, zonePalette(z));
  }

  // --- The live bar, 2px tall, sitting on the legend
  if (connected) {
    int width = (int)(barFraction(bpm) * PANEL_WIDTH + 0.5f);
    if (width < 1) width = 1; // always show something so it never reads as "off"
    display->fillRect(0, BAR_TOP_Y, width, 2, zColor);
  }
}

void drawStepsScreen(unsigned long steps) {
  display->clearScreen();
  uint16_t color = display->color565(0, 180, 255);
  display->setTextColor(color);
  display->setTextSize(1);
  display->setCursor(2, 4);
  display->print("STEPS");
  display->setTextSize(2);
  display->setCursor(2, 16);
  display->print(steps);
}

void drawDbScreen(int dbLevel) {
  display->clearScreen();
  uint16_t color = display->color565(255, 100, 255);
  display->setTextColor(color);
  display->setTextSize(1);
  display->setCursor(2, 4);
  display->print("VOLUME");

  // simple bar graph
  int barWidth = map(dbLevel, 0, 100, 0, 60);
  display->fillRect(2, 18, barWidth, 8, color);
  display->drawRect(2, 18, 60, 8, display->color565(80, 80, 80));
}
