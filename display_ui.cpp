#include "display_ui.h"
#include "config.h"

MatrixPanel_I2S_DMA *display = nullptr;

namespace {

unsigned long lastBeatTime = 0;
bool beatPhase = false;

uint16_t zoneColor(int bpm) {
  if (bpm < 100) return display->color565(0, 255, 0);    // green — resting
  if (bpm < 140) return display->color565(255, 255, 0);  // yellow — elevated
  return display->color565(255, 0, 0);                    // red — high
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
  display->setCursor(rightEdge - width, y);
  display->print(text);
}

} // namespace

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
    beatPhase = !beatPhase;
  }
}

void drawHRScreen(int bpm, bool connected) {
  display->clearScreen();
  uint16_t color = connected ? zoneColor(bpm) : display->color565(60, 60, 60);

  // Heart sits left, slightly smaller than before so a 3-digit BPM still fits
  // beside it: scale 5 spans x=1..21, leaving x=26..62 for text.
  int scale = beatPhase ? 5 : 4; // slight pulse on the beat
  drawHeart(11, 14, scale, color);

  display->setTextColor(color);
  char buf[8];
  if (connected) {
    snprintf(buf, sizeof(buf), "%d", bpm);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  printRightAligned(buf, 62, 8, 2);
  printRightAligned(connected ? "BPM" : "SCAN", 62, 24, 1);
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
