/*
  ============================================================================
  BIOHACKER BRO — FESTIVAL TOTEM
  ============================================================================
  The chest display's brain on a pole: the same ESP32-S3 board, Whoop link,
  zone math and thumb-wheel brightness, driving WS2812B grids instead of the
  64x32 HUB75 panel. Two layouts (TOTEM_RING in totem_config.h):

  FACES (default): four 16x16 panels wrapped round the head, all four showing
  the same thing at once: the chest display's zone bar on top, a heart pulsing
  on your beats, and your BPM in the zone's colour. When a friend's strap is
  connected (CREW_FACES), two opposite faces switch to them: a cyan heart beating
  on their beats; bar, number and colours otherwise the same as yours.

  RING (TOTEM_RING 1, kept for later): three 16x16 panels as one 48x16 ring. Top: an EKG
  trace that sweeps round the ring and spikes on every real heartbeat, in
  your zone colour. Bottom: a ticker drifting round it, "<3 72  DON'T DIE
  V 88  DON'T DIE ...", your BPM plus each connected friend's. When your
  Whoop is out of range the trace flatlines in grey and your rate reads "--".

  Hardware
    - WatangTech / Seengreat ESP32-S3 HUB75 board (USB-C input from the bank's USB-C1 port)
    - 3 (ring) or 4 (cube) WS2812B 16x16 flexible panels, chained, data on
      HUB75 header pin 1 (GPIO 5); 5 V from the bank's USB-A port via a USB-A
      pigtail, 18 AWG wire up the pole and lever connectors
    - Whoop strap in HR Broadcast mode, and your friends' if they're in the crew

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
#include "crew_ble.h"
#include "ring_layout.h"
#include "brightness.h"
#include "display_ui.h"

CRGB leds[NUM_LEDS];

// ---------------------------------------------------------------------------
// brightness.cpp's display hooks
// ---------------------------------------------------------------------------
TotemDisplay totemDisplay;
TotemDisplay *display = &totemDisplay;

// The wheel's brightness. What's actually shown can be lower: see
// limitBrightness().
static uint8_t wheelBrightness = BRIGHTNESS_LEVELS[BRIGHTNESS_DEFAULT_LEVEL];
void TotemDisplay::setBrightness8(uint8_t b) { wheelBrightness = b; FastLED.setBrightness(b); }

// Holds the frame under LED_MAX_MA by lowering the global brightness, based on
// whatever is in leds[] now. FastLED's own limiter does the same thing every
// frame on the frame it's showing. Measured that way, every heartbeat changed
// the result and the whole face, bar included, flickered with the pulse.
// So the face measures a frame with the heart at its peak and keeps that
// brightness for the whole beat (see loop()).
// True while a computer is on the USB-C port (see LED_MAX_MA_USB_HOST). The
// core's check can drop out for a moment on a healthy link, so a computer
// counts as gone only after 2 s with no sign of it: a single bad reading
// would otherwise flash the panels to the full cap off the Mac's port.
static bool usbHostAttached() {
  static unsigned long lastSeen = 0;
  static bool everSeen = false;
  if (Serial.isPlugged()) { lastSeen = millis(); everSeen = true; }
  return everSeen && millis() - lastSeen < 2000;
}

static uint32_t ledCapMa() { return usbHostAttached() ? LED_MAX_MA_USB_HOST : LED_MAX_MA; }

static void limitBrightness() {
  uint32_t capMw = ledCapMa() * 5;
  FastLED.setMaxPowerInMilliWatts(capMw);   // keep the backstop on the same cap
  FastLED.setBrightness(calculate_max_brightness_for_power_mW(leds, NUM_LEDS, wheelBrightness, capMw));
}

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

static float beatIntensityAt(unsigned long lastBeat) {
  float t = (float)(millis() - lastBeat);
  float i = expDecay(t, 150.0f) + 0.45f * expDecay(t - 280.0f, 120.0f);
  return i > 1.0f ? 1.0f : i;
}
static float beatIntensity() { return beatIntensityAt(lastBeatTime); }

static void advanceBeat(int bpm, unsigned long &lastBeat, unsigned long &count) {
  if (bpm <= 0) return;
  unsigned long interval = 60000UL / bpm, now = millis();
  if (now - lastBeat >= interval) {
    lastBeat += interval;
    if (now - lastBeat > interval) lastBeat = now;
    count++;
  }
}
static void updateHeartbeatPhase(int bpm) { advanceBeat(bpm, lastBeatTime, beatCount); }

// Your friend's heart keeps its own time, so the two hearts beat out of step.
static unsigned long friendLastBeat = 0, friendBeatCount = 0;

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
// The last lit pixel, where you are, blinks. `peak` is the power-measuring
// frame (see loop()): it shows that pixel lit and leaves the fill alone.
// lastFill is per person, so you and your friend each keep your own.
static void drawZoneBar(int bpm, bool connected, bool peak, int &lastFill) {
  int fill = 0;
  if (connected && bpm > 0) {
    fill = face::barFill(barFraction(bpm) * face::W, lastFill);
    if (fill < 1) fill = 1;   // always show something, as on the chest
  }
  if (!peak) lastFill = fill;
  bool blinkOff = !peak && (millis() / BAR_BLINK_MS) % 2;
  // Brightness this frame goes out at: limitBrightness() has already run for
  // the real frame. The peak frame sees last frame's, which is close enough.
  uint8_t floorV = face::visibleFloor(FastLED.getBrightness());
  for (int z = 0; z < ZONE_COUNT; z++) {
    CRGB c = zonePaletteRgb(z);
    for (int x = zoneSliceX(z, face::W); x < zoneSliceX(z + 1, face::W); x++) {
      CRGB p = c;
      if (x >= fill || (blinkOff && x == fill - 1)) p.nscale8_video(56);   // ~22%: the unlit legend
      // Never let a bar pixel round to black at low brightness: lift its
      // brightest channel to the floor, keeping the colour's proportions.
      uint8_t m = max(p.r, max(p.g, p.b));
      if (m > 0 && m < floorV) {
        p.r = (uint16_t)p.r * floorV / m; p.g = (uint16_t)p.g * floorV / m; p.b = (uint16_t)p.b * floorV / m;
      }
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

// peak: draw the brightest this frame can get (heart at the top of its beat or
// breath, blinking pixel lit), for limitBrightness() to measure.
// heartRgb: the heart's full-beat colour (red for you, cyan for your friend).
// lastBeat: when that heart last beat, for the pulse and the ripple.
// barFill: that person's bar position, kept between frames (see drawZoneBar).
static void drawFace(int bpm, bool connected, bool peak, CRGB heartRgb,
                     unsigned long lastBeat, int &barFill) {
  for (int y = 0; y < face::H; y++)
    for (int x = 0; x < face::W; x++) faceBuf[y][x] = CRGB::Black;

  int glyphs[3];
  int n;
  CRGB textColor;

  drawZoneBar(bpm, connected, peak, barFill);

  if (connected && bpm > 0) {
    // Never fully dark: still a heart between beats.
    float lvl = peak ? 1.0f : 0.18f + 0.82f * beatIntensityAt(lastBeat);
    CRGB heart = heartRgb;
    heart.nscale8_video((uint8_t)(255 * lvl));
    for (int y = 0; y < face::H; y++)
      for (int x = 0; x < face::W; x++)
        if (face::heartAt(x, y)) faceBuf[y][x] = heart;
    n = face::bpmGlyphs(bpm, glyphs);
    textColor = zoneRgb(bpm);

#if RIPPLE_ENABLED
    unsigned long age = millis() - lastBeat;
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
    float breath = peak ? 1.0f : 0.5f + 0.5f * sinf(millis() / 700.0f);
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

// ---------------------------------------------------------------------------
// Round totem: EKG trace on rows 0..8, ticker on rows 10..15
// ---------------------------------------------------------------------------
static CRGB ringBuf[ring::ROWS][ring::COLS];

// One step of the trace per column. Each column remembers the vertical run it
// drew (from the previous step's height to this one's, so the spike is a
// solid line) and when, so it can fade.
static int8_t ekgLo[ring::COLS], ekgHi[ring::COLS];
static unsigned long ekgAt[ring::COLS];
static bool ekgWritten[ring::COLS];
static int ekgHead = 0, ekgPrevY = 0;
static unsigned long ekgLastStep = 0;
static unsigned long ekgBeatSeen = 0;   // beatCount at the last spike we started
static long ekgSpikeStep = -1000;       // step index since the spike began

static void ekgStep(bool connected) {
  int y = 0;
  if (connected) {
    if (beatCount != ekgBeatSeen) { ekgBeatSeen = beatCount; ekgSpikeStep = 0; }
    y = ring::waveAt((int)ekgSpikeStep);
    ekgSpikeStep++;
  }
  ekgHead = (ekgHead + 1) % ring::COLS;
  ekgLo[ekgHead] = min(ekgPrevY, y);
  ekgHi[ekgHead] = max(ekgPrevY, y);
  ekgAt[ekgHead] = millis();
  ekgWritten[ekgHead] = true;
  ekgPrevY = y;
  for (int g = 1; g <= ring::GAP_AHEAD; g++) ekgWritten[(ekgHead + g) % ring::COLS] = false;
}

static void drawEkg(int bpm, bool connected) {
  const unsigned long stepMs = 1000UL / EKG_PX_PER_S;
  unsigned long now = millis();
  if (now - ekgLastStep > 500) ekgLastStep = now - stepMs;   // after a stall, don't race to catch up
  while (now - ekgLastStep >= stepMs) { ekgLastStep += stepMs; ekgStep(connected && bpm > 0); }

  CRGB zc = connected && bpm > 0 ? zoneRgb(bpm) : CRGB(90, 90, 90);   // grey flatline while searching
  for (int c = 0; c < ring::COLS; c++) {
    if (!ekgWritten[c]) continue;
    float age = (float)(now - ekgAt[c]);
    CRGB col = zc;
    if (c == ekgHead) {
      col = zc; col.nscale8_video(150); col += CRGB(100, 100, 100);   // the bright write head
    } else {
      col.nscale8_video((uint8_t)(255 * (0.30f + 0.70f * expf(-age / EKG_FADE_MS))));
    }
    for (int yv = ekgLo[c]; yv <= ekgHi[c]; yv++) ringBuf[ring::waveRow(yv)][c] = col;
  }
}

static CRGB crewRgb(int i) {   // a colour per friend, away from the zone colours
  static const CRGB C[CREW_MAX] = {CRGB(255, 0, 170), CRGB(40, 255, 60), CRGB(150, 60, 255), CRGB(255, 110, 0)};
  return C[i % CREW_MAX];
}

static ring::Col tickerCols[ring::MAX_COLS];

static void drawTicker(int bpm, bool connected) {
  ring::Person people[1 + CREW_MAX];
  int n = 0;
  people[n++] = ring::Person{'<', bpm, connected && bpm > 0};
  for (int i = 0; i < crewCount; i++)
    people[n++] = ring::Person{crew[i].initial, crew[i].bpm, crew[i].connected};
  int len = ring::buildTicker(people, n, tickerCols, ring::MAX_COLS);
  if (len <= 0) return;

  unsigned long now = millis();
  int off = (int)((now * TICKER_PX_PER_S / 1000UL) % (unsigned long)len);
  float beat = 0.25f + 0.75f * beatIntensity();
  for (int c = 0; c < ring::COLS - ring::SEAM_COLS; c++) {
    const ring::Col &tc = tickerCols[(c + off) % len];
    if (!tc.bits) continue;
    CRGB col;
    switch (tc.kind) {
      case ring::BADGE:
        if (tc.person == 0) {
          col = connected ? CRGB(255 * beat, 20 * beat, 20 * beat) : CRGB(70, 70, 70);   // your heart beats
        } else {
          col = crewRgb(tc.person - 1);
        }
        break;
      case ring::RATE:
        col = tc.person == 0 && !(connected && bpm > 0) ? CRGB(70, 70, 70)
              : zoneRgb(tc.person == 0 ? bpm : crew[tc.person - 1].bpm);
        break;
      default:   // DON'T DIE: a slow rainbow rolling through the letters
        col = CHSV((uint8_t)(now / 20 + (c + off) * 6), 255, 255);
    }
    for (int r = 0; r < ring::TEXT_H; r++)
      if (tc.bits & (1 << r)) ringBuf[ring::TEXT_Y + r][c] = col;
  }
}

static void drawRing(int bpm, bool connected) {
  for (int y = 0; y < ring::ROWS; y++)
    for (int x = 0; x < ring::COLS; x++) ringBuf[y][x] = CRGB::Black;
  drawEkg(bpm, connected);

  // Brightness pips replace the ticker for two seconds after a wheel change,
  // three times round so every side sees them.
  if (millis() < indicatorUntil && indicatorCount > 0) {
    for (int k = 0; k < 3; k++) {
      int x0 = k * 16 + (16 - (indicatorCount * 3 - 1)) / 2;
      for (int i = 0; i < indicatorCount; i++)
        for (int dx = 0; dx < 2; dx++)
          for (int y = 12; y < 14; y++)
            ringBuf[y][x0 + i * 3 + dx] = i <= indicatorLevel ? CRGB(220, 220, 220) : CRGB(30, 30, 30);
    }
    return;
  }
  drawTicker(bpm, connected);
}

static void blitRing() {
  for (int y = 0; y < ring::ROWS; y++)
    for (int x = 0; x < ring::COLS; x++) leds[ringLedIndex(x, y)] = ringBuf[y][x];
}

static void blitFace(int f) {
  for (int y = 0; y < face::H; y++)
    for (int x = 0; x < face::W; x++) leds[ledIndex(f, x, y)] = faceBuf[y][x];
}

static void blitAllFaces() {
  for (int f = 0; f < FACE_COUNT; f++) blitFace(f);
}

// The first friend whose strap is connected and sending, or -1.
static int liveFriend() {
  for (int i = 0; i < crewCount; i++)
    if (crew[i].connected && crew[i].bpm > 0) return i;
  return -1;
}

// Opposite sides: faces 0 and 2 show you, 1 and 3 your friend (cyan heart).
// Panels are chained round the ring in order, so even and odd faces sit
// opposite each other. With no friend connected, all four show you.
static void drawAndBlitFaces(bool peak) {
  static int youFill = 0, friendFill = 0;
  drawFace(currentBPM, hrConnected, peak, HEART_RGB_YOU, lastBeatTime, youFill);
  int fr = CREW_FACES ? liveFriend() : -1;
  if (fr < 0) { friendLastBeat = millis(); blitAllFaces(); return; }
  for (int f = 0; f < FACE_COUNT; f += 2) blitFace(f);
  if (!peak) advanceBeat(crew[fr].bpm, friendLastBeat, friendBeatCount);   // once per frame
  drawFace(crew[fr].bpm, true, peak, HEART_RGB_FRIEND, friendLastBeat, friendFill);
  for (int f = 1; f < FACE_COUNT; f += 2) blitFace(f);
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
#if TOTEM_RING
  // A white dot walking along the bottom row, column 0 to 47, once a second
  // per panel. If it jumps backwards at a panel seam, set RING_REVERSE.
  int dot = (int)((millis() / 60) % RING_COLS);
  leds[ringLedIndex(dot, face::H - 1)] = CRGB::White;
#endif
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // see biohacker_bro.ino: don't block on an unread port

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  // Kept as a backstop. limitBrightness() keeps every frame under the cap
  // already, so this never cuts in.
  FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_MAX_MA);
  // No temporal dithering. It fakes in-between levels on very dim pixels by
  // switching them on and off frame to frame. At ~30 fps, with Bluetooth
  // making frame times uneven, that showed as shimmer on the bar's dim legend.
  FastLED.setDither(DISABLE_DITHER);
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
  limitBrightness();
#else
#if TOTEM_RING
  drawRing(currentBPM, hrConnected);
  blitRing();
  limitBrightness();
#else
  // Measure the frame with the hearts at full, then draw the real one at that
  // brightness: the cap gives the same answer all through a beat.
  drawAndBlitFaces(true);
  limitBrightness();
  drawAndBlitFaces(false);
#endif
#endif
  FastLED.show();   // ~23 ms for 768 LEDs (31 ms for the cube's 1024): 30+ fps

  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 2000) {
    Serial.printf("hr=%-9s bpm=%3d  crew=", hrConnected ? "CONNECTED" : "scanning", currentBPM);
    for (int i = 0; i < crewCount; i++) Serial.printf("%c:%d ", crew[i].initial, crew[i].connected ? crew[i].bpm : 0);
    // shown: brightness actually used after the cap, out of the wheel's.
    Serial.printf(" bright=%d  shown=%d/%d  cap=%lumA%s  est_mA=%lu\r\n", brightnessLevel(),
                  FastLED.getBrightness(), wheelBrightness,
                  (unsigned long)ledCapMa(), usbHostAttached() ? "(usb)" : "",
                  (unsigned long)calculate_unscaled_power_mW(leds, NUM_LEDS) / 5);
    lastStatus = millis();
  }
  brightnessWait(2);
}
