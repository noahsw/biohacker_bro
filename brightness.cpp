#include "brightness.h"
#include "config.h"
#include "display_ui.h"

#include <Wire.h>
#include <Preferences.h>

namespace {

const int LEVEL_COUNT = sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);

// ---- The PCA9557 -----------------------------------------------------------
// Vendor wiki (https://seengreat.com/wiki/214/rgb-matrix-hub75-s3) gives the
// wheel's expander pins: K1 = IO1, K2 = IO3, K3 = IO2. It does NOT give the
// chip's I2C address, which the A0-A2 straps put somewhere in 0x18-0x1F.
// 0x18 is skipped because it's the ES8311 codec's address on this family of
// boards; the first other device answering in the range is taken to be the
// PCA9557, and the address is logged at boot so it can be confirmed.
//
// Only the input register (0) is ever READ. Nothing here writes to the chip:
// it also drives enable pins for other onboard chips, and its power-up state
// (every pin an input) is exactly what this needs.
const uint8_t PCA9557_FIRST = 0x19, PCA9557_LAST = 0x1F;
const uint8_t PCA9557_INPUT_REG = 0x00;
const uint8_t KEY_MASK = (1 << BRIGHT_KEY_UP_BIT) | (1 << BRIGHT_KEY_DOWN_BIT) |
                         (1 << BRIGHT_KEY_PRESS_BIT);

// How often the wheel is read while the loop is idle. A quick flick of the
// wheel closes its contact for only a few tens of ms: sampled once per frame
// (~19ms, and the first version managed only every OTHER frame, ~38ms) most
// flicks fell between two reads and were never seen. Each read is a ~0.1ms
// I2C transaction, so polling this often costs nothing measurable.
const unsigned long POLL_MS = 5;
// The contacts bounce on press and release; an edge inside this window of
// the last accepted one is ignored, so each click is exactly one step. Kept
// shorter than a flick, or the flick's own release would be ignored too.
const unsigned long DEBOUNCE_MS = 30;
// How long the on-screen indicator stays up after the last change.
const unsigned long INDICATOR_MS = 2000;
// Saved this long after the LAST change rather than on every one, so rolling
// through several levels costs one flash write, not one per click.
const unsigned long SAVE_DELAY_MS = 3000;

uint8_t pcaAddr = 0;      // 0 = not found; the wheel does nothing
// Pin state at boot, with nobody touching the wheel. A key counts as pressed
// when its bit differs from this, which works whichever way round the keys
// are pulled — the wiki doesn't say, and this way it doesn't have to.
uint8_t idleBits = 0;
uint8_t lastKeys = 0;
unsigned long lastEdge = 0;
// For the status line: the last raw input byte, and how many reads failed.
uint8_t lastRaw = 0;
unsigned long readErrors = 0;

Preferences prefs;
int level = BRIGHTNESS_DEFAULT_LEVEL;
bool savePending = false;
unsigned long lastChange = 0;

void onKey(int bit);

bool readInputs(uint8_t &out) {
  Wire.beginTransmission(pcaAddr);
  Wire.write(PCA9557_INPUT_REG);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(pcaAddr, (uint8_t)1) != 1) return false;
  out = Wire.read();
  return true;
}

void findPca9557() {
  for (uint8_t a = PCA9557_FIRST; a <= PCA9557_LAST; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { pcaAddr = a; return; }
  }
}

void pollWheel() {
  if (!pcaAddr) return;
  uint8_t in;
  if (!readInputs(in)) { readErrors++; return; }
  lastRaw = in;
  unsigned long now = millis();
  uint8_t keys = (in ^ idleBits) & KEY_MASK;  // 1 = held
  if (keys != lastKeys && now - lastEdge > DEBOUNCE_MS) {
    uint8_t pressed = keys & ~lastKeys;       // newly down this poll
    lastKeys = keys;
    lastEdge = now;
    for (int bit = 0; bit < 8; bit++) {
      if (pressed & (1 << bit)) onKey(bit);
    }
  }
}

void setLevel(int newLevel) {
  if (newLevel == level) {
    // Still show where we are, so rolling past the end visibly hits a stop
    // instead of looking like the wheel didn't register.
    showBrightnessIndicator(level, LEVEL_COUNT, millis() + INDICATOR_MS);
    return;
  }
  level = newLevel;
  display->setBrightness8(BRIGHTNESS_LEVELS[level]);
  showBrightnessIndicator(level, LEVEL_COUNT, millis() + INDICATOR_MS);
  savePending = true;
  lastChange = millis();
}

void onKey(int bit) {
  const char *name = bit == 1 ? "K1" : bit == 3 ? "K2" : bit == 2 ? "K3" : "?";
  if (bit == BRIGHT_KEY_UP_BIT) {
    setLevel(level < LEVEL_COUNT - 1 ? level + 1 : level);
  } else if (bit == BRIGHT_KEY_DOWN_BIT) {
    setLevel(level > 0 ? level - 1 : level);
  } else {
    setLevel((level + 1) % LEVEL_COUNT);
  }
  // Which physical motion is which K is a guess until someone rolls it on
  // the real board; this line is how to check.
  Serial.printf("wheel: %s (expander IO%d) -> level %d/%d (%d)\r\n", name, bit,
                level + 1, LEVEL_COUNT, BRIGHTNESS_LEVELS[level]);
}

} // namespace

void brightnessSetup() {
  prefs.begin("display", false);
  level = prefs.getUChar("bright", BRIGHTNESS_DEFAULT_LEVEL);
  // A LEVELS table shortened in config.h after a level was saved.
  if (level < 0 || level >= LEVEL_COUNT) level = BRIGHTNESS_DEFAULT_LEVEL;
  display->setBrightness8(BRIGHTNESS_LEVELS[level]);

  findPca9557();
  if (pcaAddr && readInputs(idleBits)) {
    Serial.printf("brightness: level %d/%d (%d), wheel on PCA9557 at 0x%02X, "
                  "idle inputs 0x%02X\r\n", level + 1, LEVEL_COUNT,
                  BRIGHTNESS_LEVELS[level], pcaAddr, idleBits);
  } else {
    pcaAddr = 0;
    Serial.printf("brightness: level %d/%d (%d), no PCA9557 found in "
                  "0x19-0x1F, wheel disabled\r\n", level + 1, LEVEL_COUNT,
                  BRIGHTNESS_LEVELS[level]);
  }
}

void brightnessWait(unsigned long ms) {
  unsigned long start = millis();
  for (;;) {
    pollWheel();
    unsigned long elapsed = millis() - start;
    if (elapsed >= ms) break;
    delay(min(POLL_MS, ms - elapsed));
  }

  if (savePending && millis() - lastChange > SAVE_DELAY_MS) {
    prefs.putUChar("bright", (uint8_t)level);
    savePending = false;
  }
}

uint8_t brightnessWheelAddr() { return pcaAddr; }
int brightnessLevel() { return level + 1; }
uint8_t brightnessWheelRaw() { return lastRaw; }
unsigned long brightnessWheelErrors() { return readErrors; }
