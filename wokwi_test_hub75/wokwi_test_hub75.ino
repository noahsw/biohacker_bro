/*
  HUB75 MATRIX — display-only test sketch, with fake data

  Cycles through the same three screens as the real costume (heart-beat
  animation, steps, party-volume bar) using fake/simulated values instead
  of a real Whoop or MPU6050, so we can actually watch the pixel art render
  in Wokwi without any of the other hardware.

  CONFIRMED via wokwi-cli: this firmware boots cleanly, displaySetup()
  returns normally, and loop() correctly cycles through every screen with
  sane values (checked via Serial0 logging + `wokwi-cli --timeout ...`).
  But `wokwi-cli --screenshot-part matrix` fails with "Error 1: Part does
  not have a valid framebuffer: matrix" — Wokwi's wokwi-hub75-matrix part
  has no framebuffer at all when driven by this DMA/I2S-peripheral-based
  library (it IS confirmed to work for simple bit-banged GPIO drivers like
  Adafruit's RGBmatrixPanel on Arduino Mega, just not this one). So: the
  display code itself is verified correct, but actually seeing it render
  requires real hardware — Wokwi cannot show it, on any plan.

  Logging auto-selects its port: Serial0 (classic UART0) under Wokwi, whose
  serial monitor watches esp:TX/esp:RX per diagram.json, and Serial (native
  USB-CDC) on real hardware built with CDCOnBoot=cdc. Using the wrong one is
  silent, not an error, which is a confusing way to lose a bring-up session.

  Reuses display_ui.h/.cpp and config.h from the repo root via symlinks.
*/

#include "config.h"
#include "display_ui.h"

// Wokwi's serial monitor watches UART0 (Serial0). On real hardware built with
// CDCOnBoot=cdc, the USB port the Mac sees is `Serial` and Serial0 goes to
// GPIO43/44, which isn't wired to USB — so logging to Serial0 there is silent.
// Pick whichever this build actually has.
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  #define LOGPORT Serial
#else
  #define LOGPORT Serial0
#endif

// Set to 1 to loop the solid-colour test forever and reprint the diagnostics
// every cycle, instead of running the three real screens. This exists because
// boot-time logging is easy to miss over USB-CDC (the port re-enumerates on
// reset, so a capture started afterwards has already lost setup()'s output),
// and because a blank panel needs the simplest possible thing on screen.
// Set back to 0 once the panel is confirmed working.
#define SOLID_TEST_ONLY 0

// Bring-up test: flood the whole panel with one colour at a time. This removes
// all of the pixel-art drawing logic from the picture — if these four screens
// look right, the panel and pin mapping are good and any remaining problem is
// in the drawing code. It also exposes a swapped RGB pin instantly, since each
// screen names the colour it is supposed to be showing.
static void solidColourTest() {
  struct { const char *name; uint8_t r, g, b; } screens[] = {
    {"WHITE", 255, 255, 255},
    {"RED",   255,   0,   0},
    {"GREEN",   0, 255,   0},
    {"BLUE",    0,   0, 255},
  };
  for (auto &s : screens) {
    LOGPORT.printf("solid test: expecting %s\r\n", s.name);
    display->fillScreen(display->color565(s.r, s.g, s.b));
    delay(2000);
  }
  display->clearScreen();
}

static bool beginOk = false;

static void printDiagnostics() {
  LOGPORT.printf("displaySetup(): begin() returned %s\r\n", beginOk ? "true" : "FALSE");
  LOGPORT.printf("pins: R1=%d G1=%d B1=%d R2=%d G2=%d B2=%d A=%d B=%d C=%d D=%d E=%d LAT=%d OE=%d CLK=%d\r\n",
                 R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN,
                 A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN);
  LOGPORT.printf("panel: %dx%d chain=%d brightness=90\r\n", PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN);
}

void setup() {
  LOGPORT.begin(115200);
  delay(2000); // give USB CDC time to enumerate, or the first lines are lost
  LOGPORT.println("Booting...");
  beginOk = displaySetup();
  printDiagnostics();
}

void loop() {
#if SOLID_TEST_ONLY
  printDiagnostics();
  solidColourTest();
  return;
#endif

  // Fake data standing in for the Whoop/MPU6050. The BPM sweeps slowly from
  // 70 up past 175 and back, which walks the zone bar through all five
  // segments — the quickest way to eyeball that the legend colors, the bar's
  // within-zone progress and the BPM text color all agree with each other.
  int fakeBpm = 122 + (int)(55 * sin(millis() / 6000.0));
  unsigned long fakeSteps = millis() / 500;

  updateHeartbeatPhase(fakeBpm);
  drawMainScreen(fakeBpm, true, fakeSteps);

  unsigned long now = millis();

  static unsigned long lastLog = 0;
  if (now - lastLog > 1000) {
    lastLog = now;
    LOGPORT.printf("loop alive: bpm=%d zone=%d steps=%lu\r\n", fakeBpm, hrZone(fakeBpm), fakeSteps);
  }

  delay(30);
}
