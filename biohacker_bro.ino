/*
  ============================================================================
  BIOHACKER BRO — Halloween Costume Display
  ============================================================================
  Hardware:
    - WatangTech ESP32-S3 HUB75 RGB Matrix Controller
    - Waveshare 64x32 RGB LED Matrix Panel (HUB75, 2.5mm pitch)
    - Whoop strap (HR Broadcast enabled in Whoop app) — read via BLE
    - GY-521 (MPU-6050) accelerometer — wired to IO45 (SDA) / IO46 (SCL)
    - Onboard mic (ES7210 codec) — for relative decibel level

  What this does:
    - Connects ONLY to your specific Whoop (filtered by MAC address, so it
      ignores anyone else's Whoop broadcasting nearby at the party)
    - Displays live BPM with a heart icon that beats in time with your
      actual heart rate, colored by HR zone (green/yellow/red)
    - Counts steps in real time using the accelerometer
    - Shows a relative "party volume" bar from the onboard mic
    - Cycles through HR / Steps / Decibels on the display

  ============================================================================
  BEFORE YOU FLASH THIS — things only YOU can fill in:
  ============================================================================
  1. TARGET_WHOOP_MAC (below) — run a BLE scan once at home (quiet room,
     only your Whoop broadcasting) and paste your Whoop's MAC address in.
     Without this, it will connect to ANY Whoop it sees first — including
     a friend's at the party.

  2. HUB75 pin mapping — the ESP32-HUB75-MatrixPanel-I2S-DMA library needs
     to know which GPIO pins on YOUR board connect to which HUB75 signal
     (R1, G1, B1, R2, G2, B2, A, B, C, D, E, CLK, LAT, OE). I do not have
     WatangTech's exact schematic, so the pin numbers below are a
     REASONABLE GUESS based on common ESP32-S3 HUB75 board layouts —
     NOT confirmed for your specific board. Check the product's wiki/
     GitHub page (WatangTech usually links one), or send me a photo of
     the board's silkscreen labels near the HUB75 header once it arrives,
     and I'll correct these.

  3. Mic I2S pins — the ES7210 audio codec on your board talks over I2S,
     but I do not have confirmed GPIO numbers for BCLK/WS/DATA on this
     board. This is genuinely vendor-specific and I don't want to guess
     wrong on something you'd have to debug blind. I've written the
     decibel code so it's ready to go the moment you have those 3 pin
     numbers — either from the board's documentation or by asking the
     seller. Until then, the dB reading will show a placeholder pattern
     instead of real audio, so the rest of the build isn't blocked on it.

  ============================================================================
*/

#include <Wire.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ============================================================================
// CONFIG — EDIT THESE
// ============================================================================

// --- 1. YOUR Whoop's MAC address (uppercase, colon-separated) ---
// TODO: replace with your real Whoop MAC after scanning at home.
#define TARGET_WHOOP_MAC "AA:BB:CC:DD:EE:FF"

// --- 2. HUB75 panel dimensions ---
#define PANEL_WIDTH  64
#define PANEL_HEIGHT 32
#define PANEL_CHAIN  1

// --- 2b. HUB75 pin mapping — UNCONFIRMED for your exact board, see note above ---
#define R1_PIN  4
#define G1_PIN  5
#define B1_PIN  6
#define R2_PIN  7
#define G2_PIN  15
#define B2_PIN  16
#define A_PIN   17
#define B_PIN   18
#define C_PIN   8
#define D_PIN   3
#define E_PIN   -1   // set to a real pin if your panel needs an E line (1/32 scan panels do)
#define LAT_PIN 40
#define OE_PIN  39
#define CLK_PIN 41

// --- 3. MPU6050 (accelerometer) — confirmed pins from earlier in build ---
#define MPU_SDA 45
#define MPU_SCL 46

// --- 4. Mic I2S pins — TODO: fill in once confirmed for your board ---
#define MIC_BCLK_PIN  -1  // TODO
#define MIC_WS_PIN    -1  // TODO
#define MIC_DATA_PIN  -1  // TODO
#define MIC_CONFIGURED false  // flip to true once the 3 pins above are filled in

// ============================================================================
// GLOBALS
// ============================================================================

MatrixPanel_I2S_DMA *display = nullptr;
Adafruit_MPU6050 mpu;

// --- Heart rate state ---
volatile int currentBPM = 0;
volatile bool hrConnected = false;
BLEAddress *targetAddress = nullptr;
BLEClient *bleClient = nullptr;
BLERemoteCharacteristic *hrCharacteristic = nullptr;
bool doConnect = false;
BLEAdvertisedDevice *foundDevice = nullptr;

unsigned long lastBeatTime = 0;
bool beatPhase = false;

// --- Step counting state ---
unsigned long stepCount = 0;
float accelBaseline = 9.8;       // roughly 1g at rest
bool stepArmed = true;
unsigned long lastStepTime = 0;
const unsigned long STEP_DEBOUNCE_MS = 250; // prevents double-counting one step
const float STEP_THRESHOLD = 1.8;           // tune this after wearing it once

// --- Decibel state ---
int currentDbLevel = 0; // 0-100 relative scale, not calibrated SPL

// --- Display cycling ---
enum DisplayMode { MODE_HR, MODE_STEPS, MODE_DB };
DisplayMode currentMode = MODE_HR;
unsigned long lastModeSwitch = 0;
const unsigned long MODE_DURATION_MS = 4000; // 4 seconds per screen

// ============================================================================
// BLE: HEART RATE CLIENT (filtered to your Whoop's MAC only)
// ============================================================================

class HRNotifyCallback {
public:
  static void onNotify(BLERemoteCharacteristic *pChar, uint8_t *data, size_t length, bool isNotify) {
    if (length < 2) return;
    uint8_t flags = data[0];
    int bpm;
    if (flags & 0x01) {
      // 16-bit BPM value
      bpm = data[1] | (data[2] << 8);
    } else {
      // 8-bit BPM value
      bpm = data[1];
    }
    currentBPM = bpm;
  }
};

class ClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *client) override {
    hrConnected = true;
  }
  void onDisconnect(BLEClient *client) override {
    hrConnected = false;
    currentBPM = 0;
  }
};

class ScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    // Only care about devices advertising the standard Heart Rate service
    if (!advertisedDevice.isAdvertisingService(BLEUUID((uint16_t)0x180D))) {
      return;
    }
    // MAC filter — this is the part that ignores your friend's Whoop
    String seenMac = advertisedDevice.getAddress().toString().c_str();
    seenMac.toUpperCase();
    String target = TARGET_WHOOP_MAC;
    target.toUpperCase();
    if (seenMac != target) {
      return; // not your Whoop — ignore it
    }
    BLEDevice::getScan()->stop();
    foundDevice = new BLEAdvertisedDevice(advertisedDevice);
    doConnect = true;
  }
};

bool connectToWhoop() {
  bleClient = BLEDevice::createClient();
  bleClient->setClientCallbacks(new ClientCallback());

  if (!bleClient->connect(foundDevice)) {
    return false;
  }

  BLERemoteService *hrService = bleClient->getService(BLEUUID((uint16_t)0x180D));
  if (hrService == nullptr) {
    bleClient->disconnect();
    return false;
  }

  hrCharacteristic = hrService->getCharacteristic(BLEUUID((uint16_t)0x2A37));
  if (hrCharacteristic == nullptr) {
    bleClient->disconnect();
    return false;
  }

  if (hrCharacteristic->canNotify()) {
    hrCharacteristic->registerForNotify(HRNotifyCallback::onNotify);
  }

  return true;
}

void bleSetup() {
  BLEDevice::init("BiohackerBro");
  BLEScan *scanner = BLEDevice::getScan();
  scanner->setAdvertisedDeviceCallbacks(new ScanCallback());
  scanner->setActiveScan(true);
  scanner->start(0, nullptr, false); // scan indefinitely until we find our Whoop
}

// ============================================================================
// STEP COUNTING (MPU6050)
// ============================================================================

void stepSetup() {
  Wire.begin(MPU_SDA, MPU_SCL);
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found — check wiring on IO45/IO46");
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }
}

void stepLoop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Magnitude of acceleration vector, minus gravity baseline
  float mag = sqrt(a.acceleration.x * a.acceleration.x +
                    a.acceleration.y * a.acceleration.y +
                    a.acceleration.z * a.acceleration.z);
  float delta = fabs(mag - accelBaseline);

  unsigned long now = millis();
  if (delta > STEP_THRESHOLD && stepArmed && (now - lastStepTime) > STEP_DEBOUNCE_MS) {
    stepCount++;
    lastStepTime = now;
    stepArmed = false;
  }
  if (delta < STEP_THRESHOLD * 0.5) {
    stepArmed = true; // re-arm once motion settles, so one step = one count
  }
}

// ============================================================================
// DECIBEL / MIC LEVEL
// ============================================================================

void micSetup() {
  if (!MIC_CONFIGURED) {
    Serial.println("Mic pins not set yet — dB readout will be a placeholder.");
    return;
  }
  // TODO once pins are confirmed: configure I2S peripheral here to read
  // from the ES7210 codec's audio output, e.g.:
  //
  //   i2s_config_t i2s_config = { ... };
  //   i2s_pin_config_t pin_config = {
  //     .bck_io_num = MIC_BCLK_PIN,
  //     .ws_io_num = MIC_WS_PIN,
  //     .data_out_num = I2S_PIN_NO_CHANGE,
  //     .data_in_num = MIC_DATA_PIN
  //   };
  //   i2s_driver_install(...);
  //   i2s_set_pin(...);
}

int micLoop() {
  if (!MIC_CONFIGURED) {
    // Placeholder: gentle fake wobble so the display isn't blank/static
    // while you wait on the real mic pins. Replace once I2S is wired up.
    return 30 + (millis() / 100) % 20;
  }

  // TODO once pins are confirmed: read a buffer of I2S samples, compute
  // RMS, and scale it to a 0-100 relative loudness value, e.g.:
  //
  //   int32_t samples[256];
  //   size_t bytesRead;
  //   i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytesRead, portMAX_DELAY);
  //   // compute RMS over `samples`, map to 0-100
  //
  return 0;
}

// ============================================================================
// DISPLAY: HEART ANIMATION + TEXT
// ============================================================================

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

void updateHeartbeatPhase() {
  if (currentBPM <= 0) return;
  unsigned long beatIntervalMs = 60000UL / currentBPM;
  unsigned long now = millis();
  if (now - lastBeatTime >= beatIntervalMs) {
    lastBeatTime = now;
    beatPhase = !beatPhase;
  }
}

void drawHRScreen() {
  display->clearScreen();
  uint16_t color = hrConnected ? zoneColor(currentBPM) : display->color565(60, 60, 60);
  int scale = beatPhase ? 6 : 5; // slight pulse on the beat
  drawHeart(16, 14, scale, color);

  display->setTextColor(color);
  display->setCursor(34, 8);
  display->setTextSize(2);
  if (hrConnected) {
    display->print(currentBPM);
  } else {
    display->print("--");
  }
  display->setTextSize(1);
  display->setCursor(34, 24);
  display->print(hrConnected ? "BPM" : "search");
}

void drawStepsScreen() {
  display->clearScreen();
  uint16_t color = display->color565(0, 180, 255);
  display->setTextColor(color);
  display->setTextSize(1);
  display->setCursor(2, 4);
  display->print("STEPS TODAY");
  display->setTextSize(2);
  display->setCursor(2, 16);
  display->print(stepCount);
}

void drawDbScreen() {
  display->clearScreen();
  uint16_t color = display->color565(255, 100, 255);
  display->setTextColor(color);
  display->setTextSize(1);
  display->setCursor(2, 4);
  display->print("PARTY VOLUME");

  // simple bar graph
  int barWidth = map(currentDbLevel, 0, 100, 0, 60);
  display->fillRect(2, 18, barWidth, 8, color);
  display->drawRect(2, 18, 60, 8, display->color565(80, 80, 80));
}

// ============================================================================
// SETUP / LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);

  // --- Matrix panel ---
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
  display->begin();
  display->setBrightness8(90); // 0-255, tune for how bright you want it all night
  display->clearScreen();

  // --- Steps ---
  stepSetup();

  // --- Mic ---
  micSetup();

  // --- BLE / Whoop ---
  bleSetup();

  lastModeSwitch = millis();
}

void loop() {
  // Handle a pending BLE connection attempt
  if (doConnect) {
    doConnect = false;
    if (!connectToWhoop()) {
      Serial.println("Failed to connect to Whoop — resuming scan.");
      BLEDevice::getScan()->start(0, nullptr, false);
    }
  }

  stepLoop();
  currentDbLevel = micLoop();
  updateHeartbeatPhase();

  // Cycle screens every few seconds
  unsigned long now = millis();
  if (now - lastModeSwitch > MODE_DURATION_MS) {
    lastModeSwitch = now;
    currentMode = (DisplayMode)((currentMode + 1) % 3);
  }

  switch (currentMode) {
    case MODE_HR:    drawHRScreen();    break;
    case MODE_STEPS: drawStepsScreen(); break;
    case MODE_DB:    drawDbScreen();    break;
  }

  delay(30); // ~30fps-ish refresh of our drawing logic
}
