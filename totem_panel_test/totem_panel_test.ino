// Totem one-panel wiring test.
//
// Lights a single 16x16 WS2812B panel wired to the HUB75 header's R1 pin, so
// the wiring can be checked before the real totem sketch (Bluetooth, Whoop,
// secrets.h) is involved. Standalone: needs only FastLED.
//
// What you should see, on a loop:
//   1. The whole panel red, then green, then blue, one second each.
//      Wrong colours (red shows green) means the panel isn't GRB order.
//   2. One white dot walking through all 256 LEDs in wiring order.
//      Where it starts and whether rows zigzag sets the PANEL_* flags in
//      totem/totem_config.h.
// The serial log names each step (python3 tools/serial_monitor.py).
//
// Dim and capped at 0.5 A on purpose, so it's safe powered from a laptop's
// USB port through the board's USB-C input, with no battery.

#include <FastLED.h>

#define LED_PIN    5     // HUB75 header pin 1 (R1), same as totem/totem_config.h
#define NUM_LEDS   256   // one 16x16 panel
#define BRIGHTNESS 20    // of 255
#define MAX_MA     500

CRGB leds[NUM_LEDS];

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // don't block when nothing reads the port
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MA);
}

void loop() {
  const CRGB colors[] = {CRGB::Red, CRGB::Green, CRGB::Blue};
  const char *names[] = {"red", "green", "blue"};
  for (int k = 0; k < 3; k++) {
    Serial.printf("panel test: solid %s\n", names[k]);
    fill_solid(leds, NUM_LEDS, colors[k]);
    FastLED.show();
    delay(1000);
  }
  Serial.println("panel test: walking dot, LED 0 to 255");
  for (int i = 0; i < NUM_LEDS; i++) {
    FastLED.clear();
    leds[i] = CRGB::White;
    FastLED.show();
    if (i % 16 == 0) Serial.printf("  dot at LED %d (row %d start)\n", i, i / 16);
    delay(15);
  }
}
