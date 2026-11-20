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
  connected (CREW_FACES), two opposite faces switch to them: a purple heart beating
  on their beats; bar, number and colours otherwise the same as yours. If your
  Whoop drops out while a friend is live, all four faces show the friend.

  SHOWS (faces layout): every 5 minutes the heart rate steps aside for one
  show, the spin and the messages taking turns, the messages in order
  (show_layout.h):
    - spin: the faces spin round the ring a couple of times, then lock back in
      (SHOW_TICKER_NUMBERS 1 adds a stock ticker of your and your friend's
      heart rates racing past)
    - messages: a phrase scrolls round the ring three times, fading in and
      out of a dark seam on one panel
    - Queen Mary: a night sea fades in and a three-funnel liner sails once
      round the ring past the moon (SHOW_SHIP 0 turns her off); she takes her
      turn after the last phrase
  SHOW_TEST_MODE plays them back to back; SHOWS_ENABLED 0 turns them off.

  PHONE (phone_ble.h): a phone page over Bluetooth can scroll your own text,
  play a show now, pause the timed shows, set the brightness, turn the
  lights off and on, and show who's connected.

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
#include "show_layout.h"
#include "phone_ble.h"
#include <esp_random.h>

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

// Lights off from the phone (POWER 0): every LED dark, Bluetooth still up so
// the phone can turn them back on. Not saved: a reboot always comes back on.
static bool lightsOn = true;
static bool wakeLights = false;   // the wheel was touched while dark: on again (see loop())

static int indicatorLevel = 0, indicatorCount = 0;
static unsigned long indicatorUntil = 0;
void showBrightnessIndicator(int level, int count, unsigned long untilMs) {
  indicatorLevel = level; indicatorCount = count; indicatorUntil = untilMs;
  // Called on every wheel touch (even rolled against the end) and the phone's
  // BRIGHT: either way someone wants to see it, so it's a way back on
  // without the phone.
  if (!lightsOn) wakeLights = true;
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
    case 0:  return CRGB(140, 140, 140);   // white, capped so it doesn't outshine yellow
    case 1:  return CRGB(255, 190, 0);     // yellow
    case 2:  return CRGB(255, 90, 0);      // orange
    default: return CRGB(255, 30, 30);     // red
  }
}

// Top row: legend dim across all 16 px, lit up to where the BPM is (0 = none).
// The last lit pixel, where you are, blinks. `peak` is the power-measuring
// frame (see loop()): it shows that pixel lit and leaves the fill alone.
// lastFill is per person, so you and your friend each keep your own.
static void drawZoneBar(int bpm, bool connected, bool peak, int &lastFill) {
  int fill = 0;
  if (connected && bpm > 0) {
    // +0.5 px: light the pixel you're IN rather than rounding to the nearest
    // edge, so the white zone reads as 60s, 70s, 80s, 90s, one pixel each.
    fill = face::barFill(barFraction(bpm) * face::W + 0.5f, lastFill);
    if (fill > face::W) fill = face::W;
    if (fill < 1) fill = 1;   // always show something, as on the chest
  }
  if (!peak) lastFill = fill;
  // The power-measuring frame lights the whole bar (see drawFace): measured on
  // the real fill, every pixel step moved the brightness of everything.
  if (peak) fill = face::W;
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
    case 0:  return CRGB(255, 255, 255);
    case 1:  return CRGB(255, 205, 0);
    case 2:  return CRGB(255, 100, 0);
    default: return CRGB(255, 30, 30);
  }
}

// ---------------------------------------------------------------------------
// Drawing: one 16x16 frame, copied to all four faces
// ---------------------------------------------------------------------------

// peak: draw the brightest this frame can get (heart at the top of its beat or
// breath, the whole bar lit, the number as a white "188"), for
// limitBrightness() to measure. Worst case rather than the real bar and digits
// on purpose: measured on the real ones, going from 69 to 70 (different lit
// pixel counts) moved the global brightness a step, and at the low levels the
// cap holds it to that visibly shifted the dim legend's colours.
// heartRgb: the heart's full-beat colour (hot pink for you, purple for your friend).
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
    n = face::bpmGlyphs(peak ? 188 : bpm, glyphs);
    textColor = peak ? CRGB(255, 255, 255) : zoneRgb(bpm);

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
    // Searching: the same heart beating at IDLE_HEART_BPM, with no number.
    // A panel that looks dead reads as broken; the missing number is enough
    // to say there's no strap. Its own beat clock, so the ring's EKG (which
    // spikes on your real beats) doesn't pick it up.
    static unsigned long idleLastBeat = 0, idleBeatCount = 0;
    if (!peak) advanceBeat(IDLE_HEART_BPM, idleLastBeat, idleBeatCount);
    float lvl = peak ? 1.0f : 0.18f + 0.82f * beatIntensityAt(idleLastBeat);
    CRGB heart = heartRgb;
    heart.nscale8_video((uint8_t)(255 * lvl));
    for (int y = 0; y < face::H; y++)
      for (int x = 0; x < face::W; x++)
        if (face::heartAt(x, y)) faceBuf[y][x] = heart;
    n = 0;
    textColor = CRGB::Black;
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

// Opposite sides: faces 0 and 2 show you, 1 and 3 your friend (purple heart).
// Panels are chained round the ring in order, so even and odd faces sit
// opposite each other. With no friend connected, all four show you.
//
// If your Whoop drops out (taken off, out of range) while a friend's strap is
// still live, all four faces show the friend instead of two idle hearts, so
// the totem keeps going while you step away. This ignores CREW_FACES: it's
// about not having dead faces, not about showing friends.
static void drawAndBlitFaces(bool peak) {
  static int youFill = 0, friendFill = 0;
  bool youLive = hrConnected && currentBPM > 0;
  int fr = liveFriend();
  if (!youLive && fr >= 0) {
    if (!peak) advanceBeat(crew[fr].bpm, friendLastBeat, friendBeatCount);   // once per frame
    drawFace(crew[fr].bpm, true, peak, HEART_RGB_FRIEND, friendLastBeat, friendFill);
    blitAllFaces();
    return;
  }
  drawFace(currentBPM, hrConnected, peak, HEART_RGB_YOU, lastBeatTime, youFill);
  if (!CREW_FACES) fr = -1;
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
  // A white dot walking along the bottom row round the ring (column 0 to 47,
  // or 63 with four panels), about a second per panel. It should move right
  // across each face (seen from outside) and carry straight on into the next
  // panel. If it jumps backwards at a panel seam, set RING_REVERSE. The
  // ticker spin needs this right; the faces don't care.
  int dot = (int)((millis() / 60) % RING_COLS);
  leds[ringLedIndex(dot, face::H - 1)] = CRGB::White;
}

// ---------------------------------------------------------------------------
// Shows: the spin and the scrolling messages (show_layout.h)
// ---------------------------------------------------------------------------
#if SHOWS_ENABLED && !TOTEM_RING
static_assert(RING_COLS == show::COLS, "shows are laid out for four 16x16 panels");
static const char *const SHOW_PHRASE_LIST[] = {SHOW_PHRASES};
static const int SHOW_PHRASE_COUNT = (int)(sizeof(SHOW_PHRASE_LIST) / sizeof(SHOW_PHRASE_LIST[0]));
// 0 = the spin, 1.. the phrases, then the ship (it takes its turn like a phrase).
static const int SHOW_SHIP_PROG = SHOW_SHIP ? 1 + SHOW_PHRASE_COUNT : -1;
static const int SHOW_PROGRAMS = 1 + SHOW_PHRASE_COUNT + (SHOW_SHIP ? 1 : 0);
static const char *const SHOW_SHIP_NAME = "QUEEN MARY";   // on the phone's button and status line

// A program's name: a phrase, or the ship's.
static const char *showProgName(int prog) {
  if (prog == 0) return "Spin";
  if (prog == SHOW_SHIP_PROG) return SHOW_SHIP_NAME;
  return SHOW_PHRASE_LIST[prog - 1];
}

static show::Player showPlayer;
static show::Frame showNormal, showFrame;
static show::HrHistory hrHist[1 + CREW_MAX];   // 0 = you, 1.. = crew slots
static bool showRunning = false;
static unsigned long showStart = 0, showNextAt = 0, hrHistAt = 0;
static int showLast = -1;
static int showLastPhrase = 0;     // 1.. : the last phrase played, so the next turn plays the one after
static uint8_t showBright = 255;   // the show's brightness, from its brightest frame
static int showFriend = -1;        // the friend in this show's ticker
static bool showsPaused = false;   // from the phone: no timed shows (playing by hand still works)
static int showPlaying = -1;       // what's on: 0 spin, 1.. a phrase (or the ship, last), -2 the phone's text
static char showCustom[show::MAX_CHARS + 1];   // the phone's text, for the status line

static Rgb toRgb(const CRGB &c) { return show::rgb(c.r, c.g, c.b); }

// Ring column block k (16 columns, left to right seen from outside) is chain
// panel ringBlockPanel(k): ringLedIndex() puts it there.
static int ringBlockPanel(int k) { return RING_REVERSE ? FACE_COUNT - 1 - k : k; }

// Copies faceBuf into the ring blocks showing you (even panels), your friend
// (odd panels) or, with who < 0, all of them.
static void faceToRing(show::Frame &ring, int who) {
  for (int k = 0; k < FACE_COUNT; k++) {
    int panel = ringBlockPanel(k);
    if (who >= 0 && panel % 2 != who) continue;
    for (int y = 0; y < face::H; y++)
      for (int x = 0; x < face::W; x++) ring[y][k * face::W + x] = toRgb(faceBuf[y][x]);
  }
}

// The heart rate display as the ring shows it now: the same faces, the same
// sides, as drawAndBlitFaces().
static void drawNormalRing(bool peak, show::Frame &ring) {
  static int youFill = 0, friendFill = 0;
  drawFace(currentBPM, hrConnected, peak, HEART_RGB_YOU, lastBeatTime, youFill);
  int fr = CREW_FACES ? liveFriend() : -1;
  if (fr < 0) { friendLastBeat = millis(); faceToRing(ring, -1); return; }
  faceToRing(ring, 0);
  if (!peak) advanceBeat(crew[fr].bpm, friendLastBeat, friendBeatCount);   // once per frame
  drawFace(crew[fr].bpm, true, peak, HEART_RGB_FRIEND, friendLastBeat, friendFill);
  faceToRing(ring, 1);
}

static void blitShowFrame(const show::Frame &f) {
  for (int y = 0; y < show::ROWS; y++)
    for (int x = 0; x < show::COLS; x++) leds[ringLedIndex(x, y)] = CRGB(f[y][x].r, f[y][x].g, f[y][x].b);
}

// You, and the friend on the faces (if any), for the ticker.
static void fillPeople(show::People &p, bool peak) {
  p.count = 1;
  p.bpm[0] = currentBPM;
  p.live[0] = hrConnected && currentBPM > 0;
  p.deltaKnown[0] = hrHist[0].delta(p.live[0] ? currentBPM : 0, p.delta[0]);
  p.zone[0] = toRgb(zoneRgb(currentBPM));
  p.heart[0] = toRgb(HEART_RGB_YOU);
  p.beat[0] = peak ? 1.0f : beatIntensity();
  int fr = showFriend;
  if (fr >= 0 && fr < crewCount) {
    p.count = 2;
    p.bpm[1] = crew[fr].bpm;
    p.live[1] = crew[fr].connected && crew[fr].bpm > 0;
    p.deltaKnown[1] = hrHist[1 + fr].delta(p.live[1] ? crew[fr].bpm : 0, p.delta[1]);
    p.zone[1] = toRgb(zoneRgb(crew[fr].bpm));
    p.heart[1] = toRgb(HEART_RGB_FRIEND);
    p.beat[1] = peak ? 1.0f : beatIntensityAt(friendLastBeat);
  }
}

// Everyone's heart rate every 20 s, for the ticker's 10-minute arrows.
static void sampleHrHistory() {
  if (hrHistAt && millis() - hrHistAt < show::HrHistory::SAMPLE_MS) return;
  hrHistAt = millis();
  hrHist[0].push(hrConnected ? currentBPM : 0);
  for (int i = 0; i < crewCount && i < CREW_MAX; i++)
    hrHist[1 + i].push(crew[i].connected ? crew[i].bpm : 0);
}

static void scheduleNextShow() {
  showNextAt = millis() + (SHOW_TEST_MODE ? SHOW_TEST_GAP_S * 1000UL : SHOW_EVERY_MIN * 60000UL);
}

// The show's brightness: the dimmest that any of its frames needs to stay
// under LED_MAX_MA, so the whole show runs at one steady level instead of
// pumping as letters come and go. Worked out a few milliseconds per frame over
// the show's first second or so (doing it all at once froze the display for
// up to 0.3 s on the longest phrase): it looks for the brightest frame by
// summing its channels, then runs FastLED's power estimate on that one alone.
// A show is one colour (the text) or the same faces shifted round (the spin),
// so the biggest sum is the frame that needs the lowest brightness. Until it's
// done the show runs at the heart rate's brightness, which is right for the
// spin (the same faces) and near enough for a phrase (black, then its first
// letters dim in the seam's fade; the scan only needs the first pass and a
// bit, Player::scanEnd(), and takes well under a second).
static const uint32_t SHOW_SCAN_BUDGET_MS = 6;   // per frame (of ~33)
static bool showScanning = false;
static uint32_t showScanT = 0, showScanBestSum = 0, showScanBestT = 0, showScanMs = 0;
static bool showScanAny = false;
static show::People showScanPeople;

static void scanShowBrightness() {
  if (!showScanning) return;
  const uint32_t start = millis();
  while (showScanT < showPlayer.scanEnd() && millis() - start < SHOW_SCAN_BUDGET_MS) {
    uint32_t t = showScanT;
    showScanT += 80;
    if (showPlayer.render(t, showNormal, showScanPeople, showFrame) <= 0) continue;
    uint32_t sum = 0;
    for (int y = 0; y < show::ROWS; y++)
      for (int x = 0; x < show::COLS; x++) sum += showFrame[y][x].r + showFrame[y][x].g + showFrame[y][x].b;
    if (!showScanAny || sum > showScanBestSum) { showScanBestSum = sum; showScanBestT = t; showScanAny = true; }
  }
  showScanMs += millis() - start;
  if (showScanT < showPlayer.scanEnd()) return;
  // Same cap as limitBrightness(): the lower USB one while a computer is
  // plugged in, if this build has it (LED_MAX_MA_USB_HOST).
#ifdef LED_MAX_MA_USB_HOST
  const uint32_t capMw = (uint32_t)ledCapMa() * 5;
#else
  const uint32_t capMw = (uint32_t)LED_MAX_MA * 5;
#endif
  uint8_t b = wheelBrightness;
  if (showScanAny) {
    showPlayer.render(showScanBestT, showNormal, showScanPeople, showFrame);
    blitShowFrame(showFrame);
    b = calculate_max_brightness_for_power_mW(leds, NUM_LEDS, wheelBrightness, capMw);
  }
  showBright = b;
  showScanning = false;
  Serial.printf("show: brightness %d/%d, worked out in %lu ms over %lu ms\r\n", showBright, wheelBrightness,
                (unsigned long)showScanMs, (unsigned long)(millis() - showStart));
}

// Starts a show now (its brightness follows, see scanShowBrightness()).
// prog: 0 = the spin, 1.. = that phrase in SHOW_PHRASES, SHOW_SHIP_PROG = the
// Queen Mary. text: scroll this instead (the phone's own text; prog is then
// ignored).
static void startShow(int prog, const char *text) {
  showFriend = CREW_FACES ? liveFriend() : -1;
  show::People p;
  fillPeople(p, true);
  const char *name;
  if (text) {
    strlcpy(showCustom, text, sizeof(showCustom));
    showPlayer.startPhrase(showCustom, SHOW_SCROLL_PX_PER_S, show::rgb(SHOW_TEXT_RGB), SHOW_REPEATS);
    showPlaying = -2;
    name = showCustom;
  } else if (prog == 0) {
    showPlayer.startTicker(p, SHOW_TICKER_PX_PER_S, SHOW_TICKER_NUMBERS, SHOW_SPIN_LAPS);
    showPlaying = 0;
    name = "ticker";
  } else if (prog == SHOW_SHIP_PROG) {
    showPlayer.startShip(esp_random(), SHOW_SHIP_PX_PER_S);
    showPlaying = prog;
    name = "ship";
  } else {
    showPlayer.startPhrase(SHOW_PHRASE_LIST[prog - 1], SHOW_SCROLL_PX_PER_S, show::rgb(SHOW_TEXT_RGB), SHOW_REPEATS);
    showPlaying = prog;
    name = SHOW_PHRASE_LIST[prog - 1];
  }

  showScanPeople = p;
  showScanT = showScanBestSum = showScanBestT = showScanMs = 0;
  showScanAny = false;
  showScanning = true;
  showRunning = true;
  showStart = millis();
  Serial.printf("show: %s, %.1f s\r\n", name, showPlayer.duration() / 1000.0f);
}

// Starts the next show in the rotation.
static bool startNextShow() {
  bool tickerOk = SHOW_TICKER && (SHOW_TEST_MODE || !SHOW_TICKER_NUMBERS || (hrConnected && currentBPM > 0));
  int prog;
  if (SHOW_TEST_MODE) {
    prog = show::nextInOrder(showLast, SHOW_PROGRAMS);
    if (prog == 0 && !SHOW_TICKER) prog = show::nextInOrder(prog, SHOW_PROGRAMS);
    if (prog == 0 && !SHOW_TICKER) prog = -1;
  } else {
    prog = show::nextAlternating(showLast, showLastPhrase, SHOW_PROGRAMS, tickerOk);
  }
  if (prog < 0) return false;
  startShow(prog, nullptr);
  showLast = prog;
  if (prog > 0) showLastPhrase = prog;
  return true;
}

static void endShow() {
  showRunning = false;
  showPlaying = -1;
  scheduleNextShow();   // the next timed one a full SHOW_EVERY_MIN after this
}

// The phone's show commands. Playing by hand leaves the rotation where it
// was, and the next timed show comes SHOW_EVERY_MIN after it ends.
static void phoneShowCommand(const phone::Command &c) {
  switch (c.kind) {
    case phone::Command::SAY:
      startShow(-1, c.text);
      break;
    case phone::Command::PLAY:
      if (c.arg >= 0 && c.arg < SHOW_PROGRAMS) startShow(c.arg, nullptr);
      break;
    case phone::Command::STOP:
      if (showRunning) endShow();
      break;
    case phone::Command::PAUSE:
      showsPaused = c.arg;
      // Pausing is for "heart rate now", so it ends a show that's on too.
      if (showsPaused && showRunning) endShow();
      if (!showsPaused) scheduleNextShow();
      Serial.printf("show: timed shows %s\r\n", showsPaused ? "paused" : "back on");
      break;
    default:
      break;
  }
}

// Lights going off end the show that's on; coming back on, the next timed
// one is a full SHOW_EVERY_MIN away rather than straight away.
static void showsLights(bool on) {
  if (!on && showRunning) endShow();
  if (on) scheduleNextShow();
}

// The show half of the phone's status line.
static void showStatusJson(phone::Out &o) {
  long left = showRunning ? ((long)showPlayer.duration() - (long)(millis() - showStart)) / 1000 : 0;
  long next = showsPaused || showRunning || !showNextAt ? -1 : ((long)(showNextAt - millis())) / 1000;
  o.add(",\"shows\":1,\"playing\":%d,\"show\":", showRunning ? showPlaying : -1);
  o.str(!showRunning ? "" : showPlaying == -2 ? showCustom : showProgName(showPlaying));
  o.add(",\"left\":%ld,\"next\":%ld,\"paused\":%d", left < 0 ? 0 : left, next < 0 ? -1 : next, showsPaused ? 1 : 0);
}

// The phrase list, for the phone's buttons (the ship last, as a button of
// its own: PLAY of its number plays it).
static void showInfoJson(phone::Out &o) {
  o.add(",\"phrases\":[");
  for (int i = 0; i + 1 < SHOW_PROGRAMS; i++) {
    const char *name = showProgName(i + 1);
    // Stop short rather than leave the list unclosed: room for this one escaped, "]" and "}".
    if (o.room() < 2 * (int)strlen(name) + 8) { o.full = true; break; }
    if (i) o.add(",");
    o.str(name);
  }
  bool fit = o.ok();
  o.full = false;
  o.add("]");
  o.full = !fit;
}

// Draws this frame of a show into leds[] and sets the brightness. False when
// no show is on: the heart rate draws as usual.
static bool showFrameDrawn() {
  sampleHrHistory();
  if (!showNextAt) scheduleNextShow();   // first one SHOW_EVERY_MIN after power-on
  if (!showRunning) {
    if (showsPaused) return false;
    if ((long)(millis() - showNextAt) < 0) return false;
    if (!startNextShow()) { showNextAt = millis() + 60000UL; return false; }
  }
  uint32_t t = millis() - showStart;
  if (t >= showPlayer.duration()) {
    endShow();
    return false;
  }
  // The heart rate's own brightness, measured as the normal loop does (hearts
  // at their peak), for the fades in and out.
  drawNormalRing(true, showNormal);
  blitShowFrame(showNormal);
  limitBrightness();
  uint8_t normalBright = FastLED.getBrightness();
  scanShowBrightness();   // wants the hearts at their peak too; uses showFrame and leds as scratch
  drawNormalRing(false, showNormal);

  show::People p;
  fillPeople(p, false);
  float mix = showPlayer.render(t, showNormal, p, showFrame);
  if (showScanning) mix = 0;   // not worked out yet: the heart rate's brightness
  blitShowFrame(showFrame);
  FastLED.setBrightness((uint8_t)(normalBright + (showBright - normalBright) * mix + 0.5f));
  return true;
}
#else
static inline bool showFrameDrawn() { return false; }
static inline void showsLights(bool) {}
static inline void sampleHrHistory() {}
static inline void phoneShowCommand(const phone::Command &) {}
static inline void showStatusJson(phone::Out &o) { o.add(",\"shows\":0"); }
static inline void showInfoJson(phone::Out &o) { o.add(",\"phrases\":[]"); }
#endif

// ---------------------------------------------------------------------------
// Phone control (phone_ble.h): commands in, status out
// ---------------------------------------------------------------------------
static void setLights(bool on) {
  if (on == lightsOn) return;
  lightsOn = on;
  showsLights(on);
  if (!on) FastLED.clear(true);
  Serial.printf("lights: %s\r\n", on ? "on" : "off");
}

static void handlePhone() {
  phone::Command c;
  while (phonePoll(c)) {
    if (c.kind == phone::Command::POWER) setLights(c.arg);
    else if (c.kind == phone::Command::BRIGHT) brightnessSetLevel(c.arg);   // turns them on too
    else {
      // Playing something by hand means you want to see it.
      if (c.kind == phone::Command::SAY || c.kind == phone::Command::PLAY) setLights(true);
      phoneShowCommand(c);
    }
  }

  // The status line a few times a second; the page reads it about once a second.
  static unsigned long lastStatus = 0;
  if (!phoneConnected() || millis() - lastStatus < 400) return;
  lastStatus = millis();
  char j[512];
  phone::Out o(j, sizeof(j) - 1);   // room kept for the closing brace
  o.add("{\"auth\":1,\"you\":{\"on\":%d,\"bpm\":%d},\"crew\":[", hrConnected ? 1 : 0, hrConnected ? currentBPM : 0);
  for (int i = 0; i < crewCount; i++)
    o.add("%s{\"i\":\"%c\",\"on\":%d,\"bpm\":%d}", i ? "," : "", crew[i].initial,
          crew[i].connected ? 1 : 0, crew[i].connected ? crew[i].bpm : 0);
  o.add("],\"bright\":%d,\"power\":%d", brightnessLevel(), lightsOn ? 1 : 0);
  showStatusJson(o);
  strcat(j, "}");
  phoneSetStatus(j);
}

static void setPhoneInfo() {
  char j[512];
  phone::Out o(j, sizeof(j) - 1);
  o.add("{\"auth\":1,\"levels\":%d,\"max\":%d", brightnessLevelCount(), phone::MAX_TEXT);
  showInfoJson(o);
  if (!o.ok()) Serial.println("phone: phrase list too long for the phone, some left off its buttons");
  strcat(j, "}");
  phoneSetInfo(j);
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
  // Phone first: NimBLE's GATT server can only take its services before the
  // straps' scanning and connections start (otherwise ble_svc_gap_init asserts
  // and the board reboots in a loop).
  setPhoneInfo();
  phoneSetup();
  bleSetup();
}

void loop() {
  bleLoop();
  handlePhone();
  updateHeartbeatPhase(currentBPM);
  if (wakeLights) { wakeLights = false; setLights(true); }

  if (!lightsOn) {
    // Dark: nothing drawn and no shows, but the heart rate history keeps
    // going so the show numbers are right once it's back on.
    sampleHrHistory();
    FastLED.clear();
    FastLED.show();   // every frame, not once: a glitched frame doesn't stay lit
  } else {
#if TOTEM_ORIENTATION_TEST
    drawOrientationTest();
    limitBrightness();
#else
#if TOTEM_RING
    drawRing(currentBPM, hrConnected);
    blitRing();
    limitBrightness();
#else
    if (!showFrameDrawn()) {
      // Measure the frame with the hearts at full, then draw the real one at that
      // brightness: the cap gives the same answer all through a beat.
      drawAndBlitFaces(true);
      limitBrightness();
      drawAndBlitFaces(false);
    }
#endif
#endif
    FastLED.show();   // ~23 ms for 768 LEDs (31 ms for the cube's 1024): 30+ fps
  }

  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 2000) {
    Serial.printf("%shr=%-9s bpm=%3d  crew=", lightsOn ? "" : "[lights off] ", hrConnected ? "CONNECTED" : "scanning", currentBPM);
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
