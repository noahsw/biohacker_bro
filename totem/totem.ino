/*
  ============================================================================
  BIOHACKER BRO — FESTIVAL TOTEM
  ============================================================================
  The chest display's brain on a pole: the same ESP32-S3 board, Whoop link,
  zone math and thumb-wheel brightness, driving a lantern of four 16x16
  WS2812B grids instead of the 64x32 HUB75 panel.

  Each face shows the same thing at once (no rotation, for the same reason as
  the costume: a passer-by gets one glance): a heart pulsing with the real
  heartbeat and the BPM in the zone's colour. When the Whoop is out of range
  (it reaches roughly 10 m, less through a crowd) the faces show a dim
  breathing heart outline and "--" until it reconnects on its own.

  Hardware
    - WatangTech / Seengreat ESP32-S3 HUB75 board (USB-C input from the bank's USB-C1 port)
    - 4x WS2812B 16x16 flexible panels, chained, data on HUB75 header pin 1
      (GPIO 5); 5 V from the bank's USB-A port via a USB-A pigtail, 18 AWG
      wire up the pole and lever connectors
    - Whoop strap in HR Broadcast mode

  Libraries: FastLED (3.9 or later, for the ESP32-S3 RMT driver), NimBLE-Arduino.
  The HUB75 DMA library is not used here.

  Build: open totem/totem.ino in the Arduino IDE (board: ESP32S3 Dev Module,
  PSRAM: OPI, USB CDC On Boot: Enabled), same as the chest sketch.
  ============================================================================
*/

#include <FastLED.h>
#include <Wire.h>

#include "config.h"
#include "totem_config.h"
#include "face_layout.h"
#include "led_map.h"
#include "hr_zones.h"
#include "ble_heart_rate.h"
#include "brightness.h"
#include "display_ui.h"

CRGB leds[NUM_LEDS];

// ---------------------------------------------------------------------------
// brightness.cpp's display hooks
// ---------------------------------------------------------------------------
TotemDisplay totemDisplay;
TotemDisplay *display = &totemDisplay;
void TotemDisplay::setBrightness8(uint8_t b) { FastLED.setBrightness(b); }

static int indicatorLevel = 0, indicatorCount = 0;
static unsigned long indicatorUntil = 0;
void showBrightnessIndicator(int level, int count, unsigned long untilMs) {
  indicatorLevel = level; indicatorCount = count; indicatorUntil = untilMs;
}

// ---------------------------------------------------------------------------
// Heartbeat envelope — the chest display's lub-dub, unchanged (see the long
// note above beatIntensity() in display_ui.cpp before touching the constants)
// ---------------------------------------------------------------------------
static unsigned long lastBeatTime = 0;
static unsigned long beatCount = 0;

static float expDecay(float t, float tau) { return t < 0 ? 0.0f : expf(-t / tau); }

static float beatIntensity() {
  float t = (float)(millis() - lastBeatTime);
  float i = expDecay(t, 150.0f) + 0.45f * expDecay(t - 280.0f, 120.0f);
  return i > 1.0f ? 1.0f : i;
}

static void updateHeartbeatPhase(int bpm) {
  if (bpm <= 0) return;
  unsigned long interval = 60000UL / bpm, now = millis();
  if (now - lastBeatTime >= interval) {
    lastBeatTime += interval;
    if (now - lastBeatTime > interval) lastBeatTime = now;
    beatCount++;
  }
}

// One 16x16 frame, drawn once and copied to all four faces.
static CRGB faceBuf[face::H][face::W];

// The chest display's legend palette, for the bar.
static CRGB zonePaletteRgb(int z) {
  switch (z) {
    case 0:  return CRGB(45, 45, 45);
    case 1:  return CRGB(140, 140, 140);
    case 2:  return CRGB(0, 110, 255);
    case 3:  return CRGB(255, 190, 0);
    default: return CRGB(255, 30, 30);
  }
}

// Top row: legend dim across all 16 px, lit up to where the BPM is (0 = none).
static void drawZoneBar(int bpm, bool connected) {
  int fill = 0;
  if (connected && bpm > 0) {
    fill = (int)(barFraction(bpm) * face::W + 0.5f);
    if (fill < 1) fill = 1;   // always show something, as on the chest
  }
  for (int z = 0; z < ZONE_COUNT; z++) {
    CRGB c = zonePaletteRgb(z);
    for (int x = zoneSliceX(z, face::W); x < zoneSliceX(z + 1, face::W); x++) {
      CRGB p = c;
      if (x >= fill) p.nscale8_video(56);   // ~22%: the unlit legend
      faceBuf[face::BAR_Y][x] = p;
    }
  }
}

// Same zone colours as the chest display's BPM digits.
static CRGB zoneRgb(int bpm) {
  switch (hrZone(bpm)) {
    case 0:
    case 1:  return CRGB(255, 255, 255);
    case 2:  return CRGB(80, 165, 255);
    case 3:  return CRGB(255, 205, 0);
    default: return CRGB(255, 30, 30);
  }
}

// ---------------------------------------------------------------------------
// Drawing: one 16x16 frame, copied to all four faces
// ---------------------------------------------------------------------------

static void drawFace(int bpm, bool connected) {
  for (int y = 0; y < face::H; y++)
    for (int x = 0; x < face::W; x++) faceBuf[y][x] = CRGB::Black;

  int glyphs[3];
  int n;
  CRGB textColor;

  drawZoneBar(bpm, connected);

  if (connected && bpm > 0) {
    float lvl = 0.18f + 0.82f * beatIntensity();   // never fully dark: still a heart between beats
    CRGB heart(255 * lvl, 20 * lvl, 20 * lvl);
    for (int y = 0; y < face::H; y++)
      for (int x = 0; x < face::W; x++)
        if (face::heartAt(x, y)) faceBuf[y][x] = heart;
    n = face::bpmGlyphs(bpm, glyphs);
    textColor = zoneRgb(bpm);

#if RIPPLE_ENABLED
    unsigned long age = millis() - lastBeatTime;
    if (age < RIPPLE_MS) {
      float r = 5.0f + 9.0f * age / RIPPLE_MS;             // ring radius in pixels
      float fade = 1.0f - (float)age / RIPPLE_MS;
      CRGB c = zoneRgb(bpm);
      c.nscale8_video((uint8_t)(70 * fade));
      for (int y = 0; y < face::H; y++)
        for (int x = 0; x < face::W; x++) {
          float dx = x - face::HEART_CX, dy = y - face::HEART_CY;
          float d = sqrtf(dx * dx + dy * dy);
          if (fabsf(d - r) < 0.7f && !faceBuf[y][x]) faceBuf[y][x] = c;
        }
    }
#endif
  } else {
    // Searching: hollow heart breathing slowly in dim white.
    float breath = 0.5f + 0.5f * sinf(millis() / 700.0f);
    uint8_t g = 40 + (uint8_t)(50 * breath);
    for (int y = 0; y < face::H; y++)
      for (int x = 0; x < face::W; x++)
        if (face::heartEdgeAt(x, y)) faceBuf[y][x] = CRGB(g, g, g);
    n = face::bpmGlyphs(0, glyphs);   // "--"
    textColor = CRGB(70, 70, 70);
  }

  // Brightness pips replace the number for two seconds after a wheel change.
  if (millis() < indicatorUntil && indicatorCount > 0) {
    int x0 = (face::W - (indicatorCount * 3 - 1)) / 2;
    for (int i = 0; i < indicatorCount; i++)
      for (int dx = 0; dx < 2; dx++)
        for (int y = 12; y < 14; y++)
          faceBuf[y][x0 + i * 3 + dx] = i <= indicatorLevel ? CRGB(220, 220, 220) : CRGB(30, 30, 30);
    return;
  }

  for (int y = 0; y < face::H; y++)
    for (int x = 0; x < face::W; x++)
      if (face::textAt(glyphs, n, x, y)) faceBuf[y][x] = textColor;
}

static void blitAllFaces() {
  for (int f = 0; f < FACE_COUNT; f++)
    for (int y = 0; y < face::H; y++)
      for (int x = 0; x < face::W; x++) leds[ledIndex(f, x, y)] = faceBuf[y][x];
}

// Orientation test: each face shows its chain number (0-3) upright in white,
// a red pixel top-left, green top-right, blue bottom-left. If a face reads
// mirrored, upside down or scrambled, change the PANEL_* flags.
static void drawOrientationTest() {
  for (int f = 0; f < FACE_COUNT; f++) {
    for (int y = 0; y < face::H; y++)
      for (int x = 0; x < face::W; x++) faceBuf[y][x] = CRGB::Black;
    int g[1] = {f};
    for (int y = 0; y < face::H; y++)
      for (int x = 0; x < face::W; x++)
        if (face::textAt(g, 1, x, y)) faceBuf[y][x] = CRGB(200, 200, 200);
    faceBuf[0][0] = CRGB::Red;
    faceBuf[0][face::W - 1] = CRGB::Green;
    faceBuf[face::H - 1][0] = CRGB::Blue;
    for (int y = 0; y < face::H; y++)
      for (int x = 0; x < face::W; x++) leds[ledIndex(f, x, y)] = faceBuf[y][x];
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // see biohacker_bro.ino: don't block on an unread port

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_MAX_MA);
  FastLED.clear(true);

  Wire.begin(MPU_SDA, MPU_SCL);   // the wheel's PCA9557 is on this bus; stepSetup() used to start it
  brightnessSetup();
  bleSetup();
}

void loop() {
  bleLoop();
  updateHeartbeatPhase(currentBPM);

#if TOTEM_ORIENTATION_TEST
  drawOrientationTest();
#else
  drawFace(currentBPM, hrConnected);
  blitAllFaces();
#endif
  FastLED.show();   // ~31 ms for 1024 LEDs, so ~30 fps; the pulse stays smooth

  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 2000) {
    Serial.printf("hr=%-9s bpm=%3d  bright=%d  est_mA=%lu\r\n",
                  hrConnected ? "CONNECTED" : "scanning", currentBPM, brightnessLevel(),
                  (unsigned long)calculate_unscaled_power_mW(leds, NUM_LEDS) / 5);
    lastStatus = millis();
  }
  brightnessWait(2);
}
