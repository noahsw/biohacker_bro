# Biohacker Bro — Halloween Costume Display

## What this is

A wearable LED display for a Halloween costume ("Biohacker Bro" — a parody
of longevity/wellness-optimization culture). It shows **live biometric
stats** on a chest-worn RGB LED matrix:

- **Heart rate** — read live from a Whoop strap over Bluetooth, displayed
  as a BPM number next to a pixel-art heart that visibly beats in time
  with the real heart rate, color-coded by zone (green/yellow/red)
- **Step count** — counted in real time from an onboard accelerometer
  (independent of the Whoop — Whoop doesn't expose steps over BLE)
- **"Party volume"** — a relative loudness meter from the board's onboard
  microphone (not a calibrated SPL/decibel meter — just a fun relative bar)

The display cycles between these three screens automatically every few
seconds. This is a costume prop, not a medical device — the goal is
"funny and easily readable across a room," not accuracy.

## Why it's built this way (context for future-me / Claude Code)

- Must be genuinely wearable for a ~5 hour house party: light-ish, no
  soldering, battery powered, no phone/app required to run.
- The wearer has multiple friends with Whoop straps who may also be at
  the party — the BLE connection MUST filter to one specific Whoop by
  MAC address, or it risks connecting to someone else's strap.
- No soldering, by requirement — every connection uses jumper wires and
  pre-assembled breakout boards.

## Hardware

| Part | Model | Notes |
|---|---|---|
| Microcontroller | WatangTech ESP32-S3 HUB75 RGB Matrix Controller | ESP32-S3-WROOM-1-N16R8: dual-core, 16MB flash, 8MB **Octal** PSRAM, WiFi + BLE 5. Has onboard ES7210 mic codec (unused for now), PCF85063 RTC, SD slot — none of that is used by this project except the mic. |
| Display | Waveshare RGB Matrix Panel, 2.5mm pitch, 64×32 pixels | HUB75 interface, connects via included ribbon cable. Wants 5V, 2.5A minimum / 4A recommended for full brightness. |
| Heart rate source | Whoop strap | HR Broadcast mode enabled in the Whoop app — broadcasts the **standard Bluetooth Heart Rate Service (0x180D)**, same protocol used by gym equipment. Whoop does NOT expose steps, HRV, recovery, etc. over this BLE service — only HR. |
| Accelerometer (steps) | GY-521 breakout (MPU-6050) | 3–5V tolerant onboard regulator. Wired to VCC (3.3V), GND, SDA→IO45, SCL→IO46. |
| Mic | Onboard ES7210 codec + dual mics | Already on the main board. I2S pins **not yet confirmed** — see Known Unknowns. |
| Power | Anker 537 PowerCore 24K (24,000mAh) | Two USB-C ports — one powers the ESP32-S3 board, the other powers the panel directly. At plain 5V (not higher PD voltages), each port maxes around 3A (~15W) — under the panel's "4A for full brightness" spec but above its 2.5A minimum, so it runs fine at slightly reduced brightness. Estimated runtime well beyond the 5-hour party (see calc below). |

**Runtime estimate:** 24,000mAh × 3.7V ≈ 88.8Wh, ~75-80Wh usable after
USB-C conversion losses. At a realistic draw for this display (dark
background, moderate brightness for a dim room) of roughly 5-10W total,
that's 7-13+ hours — comfortably past a 5-hour party. Actual draw should
be spot-checked once assembled by running it at party brightness for an
hour and checking the power bank's remaining charge.

## Wiring

**MPU6050 → ESP32-S3 board:**
| MPU6050 pin | ESP32-S3 pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | IO45 |
| SCL | IO46 |
| XDA, XCL, ADO, INT | not connected |

IO45/IO46 are also "strapping pins" on the ESP32-S3 (used briefly during
boot mode selection). In practice this is usually fine once the board is
running, but if boot ever becomes unreliable with the sensor attached,
this is the first thing to investigate.

**HUB75 panel → ESP32-S3 board:** connects via the included ribbon cable
between the board's HUB75 header and the panel's "IN" port. **Exact pin
mapping is not yet confirmed** — see Known Unknowns below.

## Software architecture

Single Arduino sketch (`biohacker_bro.ino`), organized into independent
subsystems:

1. **BLE heart rate client** — scans for BLE devices advertising service
   `0x180D`, filters to one hardcoded MAC address (the wearer's own
   Whoop, found via a one-time scan beforehand), connects, and subscribes
   to notifications on characteristic `0x2A37` (Heart Rate Measurement).
   Standard GATT heart rate parsing (1 or 2 byte BPM depending on flags
   byte).
2. **Step counter** — reads the MPU6050 via I2C (`Adafruit_MPU6050`
   library), computes acceleration magnitude, and counts a step whenever
   the magnitude deviates from the ~1g baseline past a threshold, with a
   debounce window to avoid double-counting.
3. **Decibel/mic level** — placeholder pending I2S pin confirmation (see
   below); currently outputs a fake wobble so the rest of the system
   isn't blocked on it.
4. **Display rendering** — `ESP32-HUB75-MatrixPanel-I2S-DMA` library
   (Arduino Library Manager listing name: **"ESP32 HUB75 LED MATRIX PANEL
   DMA Display"** by MrCodetastic — the GitHub repo was renamed from
   `-I2S-DMA` to `-DMA`, which is why searching the old name in Library
   Manager returns nothing). Draws a pixel-art heart that scales slightly
   on each beat (timed from real BPM), zone-colored, plus BPM/steps/dB
   text screens that cycle every 4 seconds.

## Known unknowns / TODOs (be upfront about these — don't guess silently)

- **HUB75 pin mapping**: the sketch currently has a *placeholder* pin
  assignment based on common ESP32-S3 HUB75 board layouts, NOT confirmed
  against WatangTech's actual schematic. Needs verification from the
  product's documentation/wiki or the board's silkscreen labels once
  physically in hand.
- **Mic I2S pins** (BCLK/WS/DATA for the ES7210 codec): vendor-specific,
  not yet known. The decibel subsystem is written to be easy to complete
  once these are found, but shouldn't be guessed at.
- **Step detection threshold** (`STEP_THRESHOLD` in code): a starting
  guess. Needs real-world tuning once worn, since stride and mounting
  position affect it.

## Testing approach

Real hardware hadn't arrived yet during initial development, so testing
was split:

- **Compile-checking**: Arduino IDE's Verify function catches syntax/
  library issues without any hardware connected — first line of defense.
- **Wokwi simulation** (browser-based, wokwi.com): used a forked public
  ESP32+HUB75 demo project to validate the *display logic* (heart
  animation, beat timing, zone coloring, screen cycling) and a virtual
  MPU6050 to validate *step-counting logic*, using a faked oscillating
  BPM value in place of a real Whoop (BLE to a real nearby device can't
  be simulated). See `wokwi_test.ino` for the simulator-only variant —
  it is NOT the real firmware, just a logic-testing harness.
- **BLE/Whoop connection and MAC filtering** could not be tested until
  real hardware + the real Whoop strap were both available.

## Setup (Arduino IDE on macOS)

1. Install Arduino IDE.
2. Boards Manager → install **"esp32" by Espressif Systems** (NOT
   "Arduino ESP32 Boards" — that one only covers the Arduino Nano ESP32).
3. Library Manager → install **"ESP32 HUB75 LED MATRIX PANEL DMA
   Display"** by MrCodetastic (pulls in Adafruit GFX as a dependency),
   plus **Adafruit MPU6050** and **Adafruit Unified Sensor**.
4. Board settings: **ESP32S3 Dev Module**, USB CDC On Boot: **Enabled**,
   Flash Size: **16MB**, PSRAM: **OPI PSRAM** (this board uses Octal, not
   Quad), Partition Scheme: one of the 16M options with a few MB of app
   space.
5. Before flashing: fill in `TARGET_WHOOP_MAC` in the sketch with the
   real Whoop's MAC address (found via a one-time BLE scan in a quiet
   room, e.g. using the LightBlue or nRF Connect app).
