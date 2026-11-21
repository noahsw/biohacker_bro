#include "audio_drop.h"

#include <Wire.h>
#include <ESP_I2S.h>

#include "totem_config.h"
#include "led_map.h"
#include "brightness.h"
#include "drop_detect.h"
#include "drop_fx.h"

namespace {

// ---- Pins and addresses (see audio_drop.h) ---------------------------------
const int PIN_MCLK = 38, PIN_BCLK = 48, PIN_LRCK = 21, PIN_DIN = 47;
const uint8_t ES7210_ADDR = 0x40;
const uint8_t PCA_OUTPUT_REG = 0x01, PCA_CONFIG_REG = 0x03, PCA_AUDIO_PWR_BIT = 5;

const int GAIN_STEP_DB = 3;   // ES7210 PGA: 0-33 dB in 3 dB steps (codes 0-11)

// ---- State shared between loop() and the audio task ------------------------
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
drop::Snapshot shared;          // the task's latest, copied out under mux
volatile int gainStep = 0;      // applied to the codec (loop() writes it)
volatile int wantStep = 0;      // what the task would like
volatile bool gainChanged = false;
volatile uint32_t readErrors = 0;

bool enabled = DROP_ENABLED || DROP_TEST_MODE;
bool started = false, ok = false;
const char *failWhy = "";
I2SClass i2s;
drop::Detector det;             // only the task touches it after start

// loop()'s side.
uint32_t seenDrops = 0, seenBlocked = 0, burstStart = 0, ringStart = 0, lastDrawMs = 0;
bool bursting = false, ringOn = false;
dropfx::Meter meter;
uint32_t meterMs = 0, lastLog = 0;
dropfx::Frame frame;

// ---- I2C -----------------------------------------------------------------------
bool writeReg(uint8_t addr, uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

bool readReg(uint8_t addr, uint8_t reg, uint8_t &v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return false;
  v = Wire.read();
  return true;
}

// Sets only the bits in mask, leaving the rest of the register as it was.
bool updateReg(uint8_t addr, uint8_t reg, uint8_t mask, uint8_t v) {
  uint8_t cur;
  if (!readReg(addr, reg, cur)) return false;
  return writeReg(addr, reg, (cur & ~mask) | (v & mask));
}

// Audio power on: PCA9557 IO5 an output, driven high. Read-modify-write, so
// the wheel's pins (inputs) and every other pin stay as they were.
bool audioPowerOn() {
  uint8_t pca = brightnessWheelAddr();
  if (!pca) { failWhy = "no PCA9557 (audio power switch) on I2C"; return false; }
  const uint8_t m = 1 << PCA_AUDIO_PWR_BIT;
  if (!updateReg(pca, PCA_OUTPUT_REG, m, m) || !updateReg(pca, PCA_CONFIG_REG, m, 0)) {
    failWhy = "couldn't write the PCA9557";
    return false;
  }
  return true;
}

bool es7210SetGain(int step) {
  uint8_t v = 0x10 | (uint8_t)step;   // bit 4: PGA on; bits 0-3: gain code
  return writeReg(ES7210_ADDR, 0x43, v) && writeReg(ES7210_ADDR, 0x44, v);
}

// ES7210 as an I2S slave, 16 kHz from a 4.096 MHz MCLK (256 x fs), 16-bit
// I2S, mics 1 and 2 on, 3 and 4 powered down. The register sequence is
// Espressif's own ES7210 driver's (esp_codec_dev), the one their ESP32-S3
// boards with this chip use.
bool es7210Init(int step) {
  struct { uint8_t r, v; } seq[] = {
    {0x00, 0xFF}, {0x00, 0x41},                // reset, then run
    {0x01, 0x3F},                               // all clocks off while setting up
    {0x09, 0x30}, {0x0A, 0x30},                 // power-up timing
    {0x23, 0x2A}, {0x22, 0x0A},                 // ADC1/2 high-pass filter
    {0x20, 0x0A}, {0x21, 0x2A},                 // ADC3/4 high-pass filter
    {0x40, 0x43},                               // analog: VMID etc.
    {0x41, 0x70}, {0x42, 0x70},                 // mic bias 2.87 V
    {0x07, 0x20},                               // OSR
    {0x02, 0x81},                               // MCLK: adc_div 1, DLL on (4.096 MHz -> 16 kHz)
    {0x04, 0x01}, {0x05, 0x00},                 // LRCK = MCLK / 256
    {0x11, 0x60}, {0x12, 0x00},                 // 16-bit, I2S, no TDM
    {0x4B, 0xFF}, {0x4C, 0xFF},                 // mic power: all down...
    {0x01, 0x34},                               // ...ADC1/2 clocks on (0x3F & ~0x0B)
    {0x4B, 0x00},                               // mics 1/2 powered
    {0x06, 0x00},                               // power down off
    {0x47, 0x08}, {0x48, 0x08}, {0x49, 0x08}, {0x4A, 0x08},
    {0x45, 0x00}, {0x46, 0x00},                 // mics 3/4: PGA off
    {0x00, 0x71}, {0x00, 0x41},                 // start
  };
  for (auto &s : seq) {
    if (!writeReg(ES7210_ADDR, s.r, s.v)) { failWhy = "no ES7210 (mic chip) at 0x40"; return false; }
    if (s.r == 0x21 && !updateReg(ES7210_ADDR, 0x08, 0x01, 0x00)) {   // bit 0 clear: slave mode
      failWhy = "ES7210 mode register";
      return false;
    }
  }
  if (!es7210SetGain(step)) { failWhy = "couldn't set the mic gain"; return false; }
  return true;
}

drop::Params params() {
  drop::Params p;
  p.quietDb = DROP_QUIET_DB;
  p.buildMinS = DROP_BUILD_MIN_S;
  p.jumpDb = DROP_JUMP_DB;
  p.cooldownS = DROP_COOLDOWN_S;
  p.minLevelDb = DROP_MIN_LEVEL_DB;
  return p;
}

int clampStep(int s) {
  const int lo = DROP_GAIN_MIN_DB / GAIN_STEP_DB, hi = DROP_GAIN_MAX_DB / GAIN_STEP_DB;
  return s < lo ? lo : s > hi ? hi : s > 11 ? 11 : s < 0 ? 0 : s;
}

// ---- The audio task (core 0, alongside Bluetooth) ---------------------------
void audioTask(void *) {
  static int16_t buf[drop::Detector::BLOCK * 2];
  int stepNow = gainStep;
  for (;;) {
    size_t got = i2s.readBytes((char *)buf, sizeof(buf));
    // A failing read can return at once rather than after its timeout. This
    // task outranks the Bluetooth one on core 0, so without the pause a dead
    // I2S link would spin here and starve the strap reconnects.
    if (got < sizeof(buf)) { readErrors++; vTaskDelay(pdMS_TO_TICKS(10)); continue; }
    if (gainChanged) { gainChanged = false; stepNow = gainStep; det.noteGainChange(); }
    det.feed(buf, drop::Detector::BLOCK, (float)(stepNow * GAIN_STEP_DB));
    int adv = DROP_AUTO_GAIN ? det.gainAdvice() : 0;
    taskENTER_CRITICAL(&mux);
    shared = det.snapshot();
    if (adv) wantStep = clampStep(stepNow + adv);
    taskEXIT_CRITICAL(&mux);
  }
}

void start() {
  if (started) return;
  started = true;
  int step = clampStep(DROP_MIC_GAIN_DB / GAIN_STEP_DB);
  gainStep = wantStep = step;
  ok = audioPowerOn();
  if (ok) {
    delay(20);   // let the regulator come up
    i2s.setPins(PIN_BCLK, PIN_LRCK, -1, PIN_DIN, PIN_MCLK);
    if (!i2s.begin(I2S_MODE_STD, drop::Detector::FS, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
      ok = false;
      failWhy = "I2S didn't start";
    }
  }
  if (ok) ok = es7210Init(step);   // MCLK is running now
  if (ok) {
    det.begin(params());
    xTaskCreatePinnedToCore(audioTask, "drop", 4096, nullptr, 2, nullptr, 0);
    Serial.printf("drop: mics on, gain %d dB, %s%s\r\n", step * GAIN_STEP_DB, enabled ? "detection on" : "detection off",
                  DROP_TEST_MODE ? ", TEST MODE (meter)" : "");
  } else {
    Serial.printf("drop: mics FAILED: %s. Drop detection stays off.\r\n", failWhy);
  }
}

const char *phaseName(const drop::Snapshot &s) {
  if (s.phase == drop::WARMUP) return "learning";
  if (s.phase == drop::QUIET) return "BASS OUT";
  if (s.candidate) return "bass out?";
  return "listening";
}

} // namespace

void dropSetup() {
  if (DROP_ENABLED || DROP_TEST_MODE) start();
}

void dropSetEnabled(bool on) {
  enabled = on;
  if (on) start();
  if (!on) { bursting = false; ringOn = false; }
}

int dropRingAgeMs() {
  if (!ringOn) return -1;
  uint32_t age = millis() - ringStart;
  if (age >= DROP_RING_MS + (DROP_RING_COUNT - 1) * DROP_RING_GAP_MS) { ringOn = false; return -1; }
  return (int)age;
}

bool dropEnabled() { return enabled; }

bool dropDraw(CRGB *leds, uint8_t wheelBrightness, uint32_t capMw, bool hrShowing) {
  if (DROP_DEMO && hrShowing && !ringOn && !bursting) {   // dialling in the look: a drop every DROP_DEMO_MS
    static uint32_t lastDemo = 0;
    uint32_t t = millis();
    if (t - lastDemo >= DROP_DEMO_MS) {
      lastDemo = t;
      if (DROP_ANIM == 1) { bursting = true; burstStart = t; }
      else { ringOn = true; ringStart = t; }
    }
  }
  if (!started || RING_COLS != dropfx::COLS) return false;   // four-panel layout only
  if (!enabled && !DROP_TEST_MODE) return false;

  drop::Snapshot s;
  int want = 0;
  taskENTER_CRITICAL(&mux);
  s = shared;
  want = wantStep;
  taskEXIT_CRITICAL(&mux);

  // Mic gain: the task asks, loop() does the I2C (the wheel uses the bus too).
  if (ok && want != gainStep) {
    if (es7210SetGain(want)) {
      Serial.printf("drop: mic gain %d -> %d dB (%s)\r\n", gainStep * GAIN_STEP_DB, want * GAIN_STEP_DB,
                    want < gainStep ? "it was clipping" : "it was very quiet");
      gainStep = want;
      gainChanged = true;
    }
  }

  uint32_t now = millis();
  // Not called for a while (lights off): a drop from then is stale.
  bool fresh = now - lastDrawMs < 200;
  lastDrawMs = now;
  if (ok && s.drops != seenDrops) {
    seenDrops = s.drops;
    const char *why = "";
    if (DROP_TEST_MODE) { bursting = true; burstStart = now; }
    else if (!enabled) why = " (bursts off)";
    else if (!fresh) why = " (ignored: lights were off)";
    else if (!hrShowing) why = " (ignored: a show is on)";
    else if (DROP_ANIM == 1) { bursting = true; burstStart = now; }
    else { ringOn = true; ringStart = now; }
    if (s.lastSurge)
      Serial.printf("drop: DROP! bass surged after a gap, +%.1f dB on it (drop %lu)%s\r\n", s.lastJumpDb,
                    (unsigned long)s.drops, why);
    else
      Serial.printf("drop: DROP! bass back +%.1f dB after %.1f s out (drop %lu)%s\r\n", s.lastJumpDb, s.lastBuildS,
                    (unsigned long)s.drops, why);
  }
  if (ok && s.blocked != seenBlocked) {
    seenBlocked = s.blocked;
    Serial.printf("drop: a drop, skipped: %.0f s of cooldown left\r\n", s.cooldownS);
  }
  if (!hrShowing && !DROP_TEST_MODE) { bursting = false; ringOn = false; }   // a show took over

  bool drew = false;
  if ((DROP_TEST_MODE || DROP_LOG) && now - lastLog >= 250) {
    lastLog = now;
    Serial.printf("drop: %-9s bass %6.1f dB  base %6.1f (%+5.1f)  all %6.1f / %6.1f  peak %5.1f dBFS%s  gain %d  out %4.1f s"
                  "  cooldown %3.0f s  drops %lu  read_err %lu\r\n",
                  phaseName(s), s.bassDb, s.baseDb, s.bassDb - s.baseDb, s.fullDb, s.fullBaseDb, s.peakDb,
                  s.clipping ? " CLIP" : "", gainStep * GAIN_STEP_DB, s.quietS, s.cooldownS,
                  (unsigned long)s.drops, (unsigned long)readErrors);
  }
  if (DROP_TEST_MODE) {
    if (s.ms != meterMs) { meterMs = s.ms; meter.push(s); }
    dropfx::meter(meter, s, DROP_QUIET_DB, gainStep * GAIN_STEP_DB, now, ok, frame);
    drew = true;
  } else if (bursting) {
    for (int y = 0; y < dropfx::ROWS; y++)
      for (int x = 0; x < dropfx::COLS; x++) {
        const CRGB &c = leds[ringLedIndex(x, y)];
        frame[y][x] = {c.r, c.g, c.b};
      }
  }

  if (bursting) {
    if (dropfx::burst(now - burstStart, frame)) drew = true;
    else bursting = false;
  }
  if (!drew) return false;

  for (int y = 0; y < dropfx::ROWS; y++)
    for (int x = 0; x < dropfx::COLS; x++) leds[ringLedIndex(x, y)] = CRGB(frame[y][x].r, frame[y][x].g, frame[y][x].b);
  FastLED.setBrightness(calculate_max_brightness_for_power_mW(leds, NUM_LEDS, wheelBrightness, capMw));
  return true;
}

void dropPrintStatus() {
  if (!started || (!enabled && !DROP_TEST_MODE)) return;
  if (!ok) { Serial.printf("drop: mics FAILED: %s\r\n", failWhy); return; }
  drop::Snapshot s;
  taskENTER_CRITICAL(&mux);
  s = shared;
  taskEXIT_CRITICAL(&mux);
  Serial.printf("drop: %s, bass %+.0f dB vs usual, gain %d dB, %lu drops so far%s\r\n", phaseName(s),
                s.bassDb - s.baseDb, gainStep * GAIN_STEP_DB, (unsigned long)s.drops, enabled ? "" : " (bursts off)");
}
