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
| Power | Anker 537 PowerCore 24K (24,000mAh) | Two USB-C ports feed the controller board's **two** USB-C inputs, which are separate rails ("Board" + "Panel"). The panel is then fed from the board's **VH-4P (3.96mm) 5V/4A output** — that is the board's designed panel path, and it keeps the panel's current off the ESP32's rail. The panel asks 5V/2.5A min via its VH4 header, so the 4A output covers it. At plain 5V (not higher PD voltages) each Anker port maxes around 3A (~15W). Estimated runtime well beyond the 5-hour party (see calc below). |

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
between the board's HUB75 header and the panel's **"IN"** port (the panel's
second header is an OUT for chaining). The controller's "2x HUB75" is just
two connector styles for the same signals — a boxed header and a direct-plug
header — so use whichever fits the ribbon. GPIO mapping is confirmed and
lives in `config.h`; see Known Unknowns below for the source and the two
traps (G1 is the lower GPIO, and E stays -1 on a 1/16-scan panel).

**Panel power:** not from the ribbon — via the board's VH-4P 5V/4A output to
the panel's VH4 input. See the Power row in the hardware table.

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
3. **Decibel/mic level** — I2S pins are now known but untested, and the
   ES7210 still needs register init, so this remains a placeholder that
   outputs a fake wobble (see Known Unknowns) rather than real audio. The
   rest of the system is not blocked on it.
4. **Display rendering** — `ESP32-HUB75-MatrixPanel-I2S-DMA` library
   (Arduino Library Manager listing name: **"ESP32 HUB75 LED MATRIX PANEL
   DMA Display"** by MrCodetastic — the GitHub repo was renamed from
   `-I2S-DMA` to `-DMA`, which is why searching the old name in Library
   Manager returns nothing). Draws a pixel-art heart that scales slightly
   on each beat (timed from real BPM), zone-colored, plus BPM/steps/dB
   text screens that cycle every 4 seconds.

## Known unknowns / TODOs (be upfront about these — don't guess silently)

- **HUB75 pin mapping**: RESOLVED. The board is sold under the WatangTech
  name but its model is Seengreat's **"RGB Matrix HUB75 S3"** — confirmed
  against the controller's spec sheet (same model name, ESP32-S3-WROOM-1
  -N16R8, 16MB/8MB, 2x USB-C in, VH-4P 5V/4A out, 2x HUB75, ES7210 + ES8311,
  SD + PCF85063 RTC). That model's wiki publishes the GPIO mapping:
  https://seengreat.com/wiki/214/rgb-matrix-hub75-s3 — `config.h` now uses
  it, **with one correction made on hardware: the wiki has G and B
  transposed.** It publishes G1=IO4 / B1=IO6 and G2=IO7 / B2=IO17, but with
  those values a full-screen green renders blue and blue renders green, on
  both row halves. `config.h` swaps them. Everything else in that table (R,
  A-E, CLK, LAT, OE) was correct as published. This error is invisible unless
  you test named colours — `begin()` succeeds, geometry is perfect, text is
  legible — so anyone following that page ships with green and blue swapped
  and no sign anything is wrong.
  `E_PIN` stays -1: E is wired to IO16, but the Waveshare panel is 1/16 scan
  (per its own user guide) and only uses A-D.
- **Mic I2S pins**: now known from the same wiki (MCLK=38, BCLK=48, WS=21,
  mic data in / SDOUT=47), but untested — and the ES7210 almost certainly
  needs I2C register setup before it streams anything, so `MIC_CONFIGURED`
  stays `false` until real audio is confirmed.
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
   plus **Adafruit MPU6050**, **Adafruit Unified Sensor**, and
   **NimBLE-Arduino** by h2zero (tested on 2.5.1 — required, see the
   Whoop/MTU note under Gotchas).
4. Board settings: **ESP32S3 Dev Module**, USB CDC On Boot: **Enabled**,
   Flash Size: **16MB**, PSRAM: **OPI PSRAM** (this board uses Octal, not
   Quad), Partition Scheme: one of the 16M options with a few MB of app
   space.
5. Before flashing: copy `secrets.h.example` to `secrets.h` and fill in
   the real Whoop MAC (found by flashing `find_whoop_mac/` and watching
   Serial Monitor at 115200). `secrets.h` is gitignored.

   On Noah's machine the real file lives at
   `~/.config/biohacker_bro/secrets.h` and each worktree symlinks to it;
   the SessionStart hook in `.claude/settings.json` creates that symlink
   automatically. See `secrets.h.example`.

## Building and flashing from the command line

Faster than the IDE for iterating, and it is how the board was brought up:

```
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
arduino-cli compile -b "$FQBN" wokwi_test_hub75
arduino-cli upload  -b "$FQBN" -p /dev/cu.usbmodem201101 wokwi_test_hub75
```

Find the port with `arduino-cli board list`. That FQBN encodes the same
settings as the IDE's Tools menu, so keep the two in sync.

To watch serial, note that `arduino-cli monitor` did not reliably capture
this board's USB-CDC output; reading the device directly did:

```
stty -f /dev/cu.usbmodem201101 115200 raw && cat /dev/cu.usbmodem201101
```

## Gotchas found on real hardware

**The Whoop connects, but the bundled ESP32 BLE library says it failed.**
The core's built-in `BLE` library always calls `ble_gattc_exchange_mtu()`
as soon as the link comes up. The Whoop performs the MTU exchange itself
the instant it connects, so that call returns `BLE_HS_EALREADY`
(`status=2`), which the bundled library treats as a fatal error — while
leaving the BLE link open. Every retry then fails instantly with
`Client busy, connected to ...` against its own live connection, so it
looks like the Whoop is refusing you when it is actually already
connected. (The link reported `MTU=247` already negotiated, which is the
proof.)

The fix, and the reason for the NimBLE-Arduino dependency, is
`client->connect(&device, true, false, /*exchangeMTU=*/false)` in
`ble_heart_rate.cpp`.

Two things this is NOT, both of which were ruled out by instrumenting the
failure rather than guessing:
- It is not your phone holding the connection. Whoop's HR Broadcast
  serves several centrals at once — that is why a Peloton shows your HR
  while the Whoop app is open. Leave your phone's Bluetooth on.
- It is not a connectability or address-type problem. The strap
  advertises `advType=0 CONN_ADV` with `addrType=1` (random static), and
  the library handles that correctly.

**The panel's data input is J2, not J1.** The Waveshare panel has two HUB75
headers, one input and one output for chaining, and on this panel the input is
the one silkscreened **J2**. J1 is the output. Plugging into J1 gives a
completely blank panel with no error of any kind — the driver initializes
fine, `begin()` returns true, and the firmware loops happily, because nothing
downstream reports back. Cost a full debug session; check this first if the
panel is dark.

**Bring-up diagnostics live in `wokwi_test_hub75/`.** Set `SOLID_TEST_ONLY 1`
to loop a full-screen white/red/green/blue cycle forever and reprint the pin
table each pass. Two reasons it exists: it removes all the pixel-art drawing
logic from the picture when the panel is misbehaving, and because it repeats
forever you can move cables and watch the result live instead of reflashing
between attempts. A swapped RGB pin shows up immediately, since each screen
announces the colour it is supposed to be. Set back to 0 when done.

**Serial logging picks its port at compile time.** Under Wokwi the serial
monitor watches UART0 (`Serial0`); on real hardware built with
`CDCOnBoot=cdc`, the port the Mac sees is `Serial` (native USB-CDC) and
`Serial0` goes to GPIO43/44, which isn't wired to USB. Logging to the wrong
one is *silent*, not an error, which is a confusing way to lose a bring-up
session. The sketch now selects automatically on `ARDUINO_USB_CDC_ON_BOOT`.

**Catching boot-time serial output over USB-CDC is racy.** The port
re-enumerates on reset, so a capture started after the reset has already
missed `setup()`. Either reprint diagnostics from `loop()` (what
`SOLID_TEST_ONLY` does) or accept that you will miss the first lines.

**The test sketches need their own `secrets.h` symlink.** `config.h` includes
`secrets.h`, and the preprocessor resolves that relative to the directory it
found `config.h` in — so symlinking only `config.h` into a `wokwi_test_*/`
dir makes every one of those sketches fail to compile with
`fatal error: secrets.h: No such file or directory`. Each test dir needs
`secrets.h -> ../secrets.h` alongside it. `.gitignore` matches `secrets.h` at
any depth, so these symlinks are correctly ignored.

**`find_whoop_mac/` still uses the bundled BLE library**, which is fine —
scanning works there and the two libraries are only a problem if a single
sketch includes both. Do not add `NimBLEDevice.h` to that sketch.
