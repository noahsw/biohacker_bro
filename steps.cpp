#include "steps.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Preferences.h>

unsigned long stepCount = STEP_COUNT_START;

namespace {

Adafruit_MPU6050 mpu;
bool mpuOk = false;                 // false => stepLoop() is a no-op
float accelBaseline = 9.8;       // roughly 1g at rest
bool stepArmed = true;
unsigned long lastStepTime = 0;
const unsigned long STEP_DEBOUNCE_MS = 250; // prevents double-counting one step
const float STEP_THRESHOLD = 1.8;           // tune this after wearing it once

// ---- Persistence ----------------------------------------------------------
// The count survives a reboot or a battery swap by living in NVS flash, keyed
// by the calendar day read from the onboard PCF85063 RTC. Same day on boot =>
// restore; different day => start over at STEP_COUNT_START.
//
// Saves are throttled: NVS is flash, and writing on every step would burn
// through it. Unplugging loses at most STEP_SAVE_INTERVAL_MS of steps.
const uint8_t PCF85063_ADDR = 0x51;
const unsigned long STEP_SAVE_INTERVAL_MS = 15000;
const unsigned long DAY_CHECK_INTERVAL_MS = 10000;

Preferences prefs;
uint32_t currentDay = 0;            // YYYYMMDD, 0 = RTC time unknown
unsigned long savedCount = 0;
unsigned long lastSave = 0;
unsigned long lastDayCheck = 0;

uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

// Today as YYYYMMDD, shifted back DAY_ROLLOVER_HOUR hours so a night out
// still counts as the day it started. Returns 0 if the RTC is absent or its
// oscillator-stop flag says the time is garbage (it lost power).
uint32_t rtcDay() {
  Wire.beginTransmission(PCF85063_ADDR);
  Wire.write(0x04);  // seconds register; minutes..year follow
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(PCF85063_ADDR, (uint8_t)7) != 7) return 0;
  uint8_t r[7];
  for (int i = 0; i < 7; i++) r[i] = Wire.read();
  if (r[0] & 0x80) return 0;        // OS flag: clock stopped, time invalid

  struct tm t = {};
  t.tm_hour = bcd2bin(r[2] & 0x3F);
  t.tm_mday = bcd2bin(r[3] & 0x3F);
  t.tm_mon  = bcd2bin(r[5] & 0x1F) - 1;
  t.tm_year = bcd2bin(r[6]) + 100;  // 2000-based
  t.tm_hour -= DAY_ROLLOVER_HOUR;
  mktime(&t);                       // normalises a negative hour into yesterday
  return (t.tm_year + 1900) * 10000UL + (t.tm_mon + 1) * 100UL + t.tm_mday;
}

// Set the RTC from the build machine's clock. Only done on the first boot of
// a freshly flashed build, so plugging the board in to flash it is also what
// sets its date — there's no WiFi or buttons to do it any other way.
void rtcSetFromBuildTime() {
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *d = __DATE__;         // "Oct  5 2026"
  const char *tm = __TIME__;        // "07:42:13"
  uint8_t mon = (strstr(months, String(d).substring(0, 3).c_str()) - months) / 3 + 1;
  uint8_t day = atoi(d + 4);
  uint8_t yr  = atoi(d + 9);        // last two digits
  Wire.beginTransmission(PCF85063_ADDR);
  Wire.write(0x04);
  Wire.write(bin2bcd(atoi(tm + 6)));  // writing seconds also clears OS
  Wire.write(bin2bcd(atoi(tm + 3)));
  Wire.write(bin2bcd(atoi(tm)));
  Wire.write(bin2bcd(day));
  Wire.write(0);                    // weekday, unused
  Wire.write(bin2bcd(mon));
  Wire.write(bin2bcd(yr));
  Wire.endTransmission();
}

void saveSteps() {
  prefs.putULong("count", stepCount);
  prefs.putUInt("day", currentDay);
  savedCount = stepCount;
  lastSave = millis();
}

void restoreSteps() {
  prefs.begin("steps", false);

  const char *build = __DATE__ " " __TIME__;
  if (prefs.getString("build", "") != build) {
    rtcSetFromBuildTime();
    prefs.putString("build", build);
  }

  currentDay = rtcDay();
  uint32_t storedDay = prefs.getUInt("day", 0);
  bool haveStored = prefs.isKey("count");

  // Unknown date (RTC lost power): keep the stored count rather than wipe a
  // day's steps over a clock problem.
  if (haveStored && (currentDay == 0 || currentDay == storedDay)) {
    stepCount = prefs.getULong("count", STEP_COUNT_START);
    if (currentDay == 0) currentDay = storedDay;
  } else {
    stepCount = STEP_COUNT_START;
  }
  Serial.printf("steps: day=%lu stored=%lu restored=%lu\r\n",
                (unsigned long)currentDay, (unsigned long)storedDay, stepCount);
  saveSteps();
}

void persistLoop() {
  unsigned long now = millis();
  if (now - lastDayCheck > DAY_CHECK_INTERVAL_MS) {
    lastDayCheck = now;
    uint32_t today = rtcDay();
    if (today != 0 && today != currentDay) {
      currentDay = today;
      stepCount = STEP_COUNT_START;
      saveSteps();
      return;
    }
  }
  if (stepCount != savedCount && now - lastSave > STEP_SAVE_INTERVAL_MS) {
    saveSteps();
  }
}

} // namespace

void stepSetup() {
  Wire.begin(MPU_SDA, MPU_SCL);
  mpuOk = mpu.begin();
  if (!mpuOk) {
    // Print the pins rather than hardcoding them: this sensor has already
    // moved buses once (IO45/46 -> IO1/2) and a stale pin in an error message
    // sends you to check the wrong connector.
    Serial.printf("MPU6050 not found — check wiring on SDA=IO%d SCL=IO%d\r\n",
                  MPU_SDA, MPU_SCL);
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }
  restoreSteps();
}

bool stepSensorOk() { return mpuOk; }

float stepAccelDelta() {
  if (!mpuOk) return 0.0f;
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float mag = sqrt(a.acceleration.x * a.acceleration.x +
                   a.acceleration.y * a.acceleration.y +
                   a.acceleration.z * a.acceleration.z);
  return fabs(mag - accelBaseline);
}

void stepLoop() {
  // Without a sensor, getEvent() leaves the event struct untouched and we
  // would count "steps" out of uninitialized stack. Do nothing instead.
  persistLoop();
  if (!mpuOk) return;

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
