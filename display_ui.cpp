#include "display_ui.h"
#include "config.h"

MatrixPanel_I2S_DMA *display = nullptr;

// ============================================================================
// LAYOUT (64x32)
// ============================================================================
//   x0..21,  y0..28   heart, beating in time with the real BPM
//   x23..63, y3..17   BPM, size 2, in the current zone's color
//   x23..63, y21..28  step count, size 1
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
    // Deliberately dim: at full-ish gray, Z0 white and Z1 gray were nearly
    // indistinguishable on the real panel — an LED "gray" is just a dimmer
    // white, so the two segments need a big brightness gap to read apart.
    case 1:  return display->color565(55, 55, 55);   // gray
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

// Draws text ending at `rightEdge` instead of starting at a cursor, so a
// number stays inside the panel as it gains digits. 6px per char at size 1,
// scaling linearly with text size.
void printRightAligned(const char *text, int rightEdge, int y, int textSize) {
  int width = strlen(text) * 6 * textSize;
  display->setTextSize(textSize);
  display->setCursor(rightEdge - width + 1, y);
  display->print(text);
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

float beatIntensity() {
  float t = (float)(millis() - lastBeatTime);
  float i = expDecay(t, 150.0f) + 0.45f * expDecay(t - 280.0f, 120.0f);
  if (i > 1.0f) i = 1.0f;
  return i;
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

  // --- Heart: always red (it's a heart), pulsing in brightness on each beat.
  // Dimmed to a dark ember while we're still hunting for the strap, so a
  // stale reading can never look live.
  uint16_t heartColor;
  if (connected) {
    float level = 0.35f + 0.65f * beatIntensity();
    heartColor = display->color565((int)(255 * level), (int)(20 * level), (int)(20 * level));
  } else {
    heartColor = display->color565(50, 0, 0);
  }
  drawHeart(11, 12, 4, heartColor);

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

  // Frame complete — show it. With double_buff on, nothing drawn above has
  // been visible until this call.
  display->flipDMABuffer();
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
  display->flipDMABuffer();
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
  display->flipDMABuffer();
}
