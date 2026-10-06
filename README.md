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
- **HR zone bar** — a live bar along the top two rows, sitting under a
  fixed 5-segment zone legend, so the zone reads from where the fill stops

**All of it on one screen, at once — the display does not rotate.** A costume
gets about one second of a stranger's attention, and a cycling display
guarantees that second lands on the wrong screen. See the comment above
`setup()` in `biohacker_bro.ino`, and the pixel budget in `display_ui.cpp`.

It reads at two distances on purpose: the heart, the BPM digits and the zone
bar carry across a room, while the step count is small enough that it rewards
someone who comes closer. That's deliberate, not a compromise.

**The gauge sits directly above the heart rate, not at the bottom.** The bar
*is* the heart rate — the same number the digits show, placed on a scale — and
adjacency is the only thing on a 64×32 panel that says which number a gauge
belongs to. At the bottom it sat under the step count, which put a value and
its own gauge on opposite sides of an unrelated number and invited reading it
as a steps progress meter.

This is a costume prop, not a medical device — the goal is "funny and easily
readable across a room," not accuracy.

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
| Microcontroller | WatangTech ESP32-S3 HUB75 RGB Matrix Controller | ESP32-S3-WROOM-1-N16R8: dual-core, 16MB flash, 8MB **Octal** PSRAM, WiFi + BLE 5. Has an onboard ES7210 mic codec, PCF85063 RTC and SD slot, none of which this project uses. |
| Display | Waveshare RGB Matrix Panel, 2.5mm pitch, 64×32 pixels | HUB75 interface, connects via included ribbon cable. Wants 5V, 2.5A minimum / 4A recommended for full brightness. |
| Heart rate source | Whoop strap | HR Broadcast mode enabled in the Whoop app — broadcasts the **standard Bluetooth Heart Rate Service (0x180D)**, same protocol used by gym equipment. Whoop does NOT expose steps, HRV, recovery, etc. over this BLE service — only HR. |
| Accelerometer (steps) | GY-521 breakout (MPU-6050), **pre-soldered headers** | Buy the pre-soldered kind. Plugs into the board's 4-pin I2C expansion connector (IO1/IO2) via a JST-SH-to-female-sockets cable — no soldering anywhere. Run it at **3.3V**, never 5V. |
| Power | Anker 537 PowerCore 24K (24,000mAh) | **One** USB-C cable, into the board input silkscreened **`USB-C`**. That port is the one that feeds everything: per the vendor, it is "for board power, matrix power, and program download/debugging", while the second input (`Power`) is "dedicated for powering the HUB75 RGB LED matrix only". So the two inputs are NOT interchangeable — `Power` alone leaves the ESP32 unpowered and the whole thing dead, confirmed on hardware. The panel is fed from the board's **VH-4P (3.96mm) 5V/4A output** in both cases; the ribbon never carries panel power. The panel asks 5V/2.5A min via its VH4 header and 4A for full brightness, and an Anker port caps near 3A at plain 5V (not higher PD voltages) — so the second cable into `Power` exists to give the matrix its own supply when one port isn't enough. At this project's brightness it is not needed: one cable has run board + panel for over an hour at party brightness. |

**Runtime estimate:** 24,000mAh × 3.7V ≈ 88.8Wh, ~75-80Wh usable after
USB-C conversion losses. At a realistic draw for this display (dark
background, moderate brightness for a dim room) of roughly 5-10W total,
that's 7-13+ hours — comfortably past a 5-hour party. Actual draw should
be spot-checked once assembled by running it at party brightness for an
hour and checking the power bank's remaining charge.

## Wiring

**MPU6050 → ESP32-S3 board:** via the white 4-pin 1mm-pitch **I2C expansion
connector** on the left edge of the board, beside the USB-C ports — *not* the
`3V3 GND IO46 IO45` breakout along the bottom edge.

| Connector pin (top→bottom) | MPU6050 pin |
|---|---|
| 3V3 | VCC |
| GND | GND |
| IO1 (SDA) | SDA |
| IO2 (SCL) | SCL |
| — | XDA, XCL, ADO, INT: not connected |

**Why not IO45/IO46, which the bottom breakout exposes and which this project
originally used?** Because that breakout is *bare plated through-holes* — no
pin header, no socket, nothing a jumper wire can grip. Using it means
soldering the board as well as the GY-521 (whose header also ships loose in
the bag), and this build is no-solder by requirement. The expansion connector
carries the same I2C peripheral on a connector you can actually plug into.
Easy to miss from photos, since the bottom breakout is labelled so invitingly.

**The cable is a JST-SH to four loose female sockets**, not a Qwiic-to-Qwiic
cable, and that is deliberate — see the trap below.

⚠️ **This connector is not wired in Qwiic order, and a Qwiic cable will
destroy the sensor.** The silkscreen reads `3V3, GND, IO1, IO2` top to bottom.
Qwiic / STEMMA QT is `GND, V+, SDA, SCL` — power and ground transposed. Both
use the same 1mm JST-SH housing, so a standard Qwiic-to-Qwiic cable plugs in
with a satisfying latch and puts 3.3V on the sensor's ground pin. Seengreat
never claimed Qwiic compatibility (their wiki just calls it an "I2C expansion
connector"), so this is two conventions sharing a plug rather than a vendor
mistake — which makes it no less destructive. Loose sockets let you place each
wire by *function*, sidestepping the whole question.

**Meter the cable before the sensor is ever attached.** Plug the cable into a
powered board with nothing on the far end and check which wire is +3.3V and
which is 0V. Do not trust the wire colours: they follow the cable maker's
Qwiic convention, not this board's pin order, so on this connector the black
wire is sitting on 3V3 and the red on GND. Getting SDA/SCL backwards is
harmless — the sensor simply won't enumerate and the scanner in
`steps_bringup/` will say so. Getting VCC/GND backwards kills it in seconds.

**This bus is shared.** Unlike IO45/IO46, IO1/IO2 also carry the onboard
PCF85063 RTC, ES7210, ES8311 and PCA9557. The MPU6050 answers at `0x68` with
ADO floating, which none of those claim, so there's no conflict — but
`steps_bringup/` prints every address it finds, so confirm rather than assume.

**HUB75 panel → ESP32-S3 board:** connects via the included ribbon cable
between the board's HUB75 header and the panel's **"IN"** port (the panel's
second header is an OUT for chaining). The controller's "2x HUB75" is just
two connector styles for the same signals — a boxed header and a direct-plug
header — so use whichever fits the ribbon. GPIO mapping is confirmed and
lives in `config.h`; see Known Unknowns below for the source and the two
traps (G1 is the lower GPIO, and E stays -1 on a 1/16-scan panel).

**Panel power:** not from the ribbon — via the board's VH-4P 5V/4A output to
the panel's VH4 input. One USB-C cable into the board's `USB-C` input feeds
both the ESP32 and that VH-4P output. Do **not** use the `Power` input on its
own: it feeds the matrix only, so the ESP32 never boots and nothing lights up.
See the Power row in the hardware table.

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
3. **Display rendering** — `ESP32-HUB75-MatrixPanel-I2S-DMA` library
   (Arduino Library Manager listing name: **"ESP32 HUB75 LED MATRIX PANEL
   DMA Display"** by MrCodetastic — the GitHub repo was renamed from
   `-I2S-DMA` to `-DMA`, which is why searching the old name in Library
   Manager returns nothing). Draws one fixed layout: a pixel-art heart
   pulsing in brightness on each beat (timed from real BPM), the BPM in
   the current zone's hue, the step count, and the zone bar + legend.
   `drawStepsScreen()` / `drawDbScreen()` survive only for the isolated
   test sketches and are not used by the firmware.

## What got cut

**The decibel / "party volume" meter.** It was never drawn on the real
layout and is now removed from the code entirely (`mic.cpp`, `mic.h`,
`drawDbScreen()`, the `MIC_*` pins). Three reasons, in order of weight:

1. **No room.** 64×32 is ten characters of size-1 text per row. The step row
   already uses 60 of its 64 pixels once it's labelled; there is no second
   row to give away.
2. **It wants to be a bar, and the bar is taken.** Two bars on a 32px-tall
   panel compete for the same read, and neither wins.
3. **It isn't a longevity metric.** Loudness is a party gimmick, which
   dilutes the quantified-self joke the costume is making.

It was also the only subsystem never verified on hardware. The I2S pin
mapping worked out from the vendor wiki — **MCLK=IO38, BCLK/SCLK=IO48,
LRCK/WS=IO21, mic SDOUT=IO47, speaker DSDIN=IO14** — is recorded here so the
research isn't lost, but it was never tested, and the ES7210 needs I2C
register init before it would stream anything at all.

If ambient sound ever earns a place, the promising route is one that costs no
pixels: let loudness drive overall panel brightness or a background pulse, so
the display breathes with the room rather than showing a number.

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
- **Step detection threshold** (`STEP_THRESHOLD` in code): a starting
  guess. Needs real-world tuning once worn, since stride and mounting
  position affect it. `steps_bringup/` exists to make that tuning
  measurable rather than a bisection search — see below.
- **Runtime past an hour is unmeasured.** One cable into `USB-C` has run
  board + panel for an hour at the brightness the costume will actually use,
  so power *adequacy* at this operating point is settled — it is not a
  bench-brightness result that falls over on the night. What is still open
  is duration: an hour is not five, and the 7-13 hour figure above is an
  estimate from a nameplate capacity, not a measurement. Run it at this
  brightness and check the bank's remaining charge against elapsed time; that
  is the one number that turns the estimate into a fact.
  If it ever does come up short, the fix is the second cable into `Power`,
  which gives the matrix its own supply instead of sharing the `USB-C`
  port's ~3A with the ESP32.

- **The MPU6050 has not been run on real hardware yet.** The move to the
  I2C expansion connector (IO1/IO2) is reasoned from the vendor wiki and
  the board's own silkscreen, not yet confirmed by a sensor that
  enumerated. The bus assignment, the 0x68 address being free, and the
  pin order all want confirming on first power-up.

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

### Seeing the layout on the panel (`wokwi_test_display_steps/`)

Draws the real `drawMainScreen()` with a **simulated** BPM sweeping the full
zone range, so the actual layout can be checked on the actual panel with no
Whoop in range and no accelerometer attached (steps just stay at 0, and the
serial line says `mpu=ABSENT`). This is the sketch to flash while iterating on
the design.

It used to call `drawStepsScreen()`, which meant the one sketch whose job is
previewing the display never drew the layout the costume actually uses. Worse,
`--gc-sections` then dropped `drawMainScreen()` from the binary entirely, so
the compiled size didn't move when the layout changed — a quiet way to believe
a change is covered when nothing links it.

```bash
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
arduino-cli upload -b "$FQBN" -p /dev/cu.usbmodem201101 wokwi_test_display_steps
```

### Step counter bring-up (`steps_bringup/`)

Real-hardware counterpart to `wokwi_test_steps/`, which is simulator-only.
It answers the two questions bring-up actually asks:

1. **Is the sensor on the bus at all?** It scans I2C and prints every address
   it finds. You want `0x68`, alongside the onboard chips that share IO1/IO2.
2. **What should `STEP_THRESHOLD` be?** It prints the peak acceleration delta
   since the last line — the exact quantity `steps.cpp` compares against. Hold
   the sensor still to read the noise floor, walk twenty steps to read the per-
   step peak, and put the threshold between them. Beats guessing and reflashing.

Both print from `loop()`, not `setup()`, and the scan repeats every pass: USB-CDC
re-enumerates on reset so `setup()` output is usually gone before a capture
attaches, and repeating lets you reseat a connector and watch the address appear
without reflashing.

```bash
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
arduino-cli compile --upload -b "$FQBN" -p /dev/cu.usbmodem201101 steps_bringup
python3 tools/serial_monitor.py /dev/cu.usbmodem201101 45   # capture 45s
```

`upload` alone fails with "Compiled sketch not found" unless the sketch was
compiled first, hence `compile --upload`.

For a tuning run: stand still ~5s, take exactly 20 counted steps, stand still
until the capture ends. The still stretches bracket the walk in the log, so
the count across it is unambiguous. First bring-up (hand-held) landed at
still ≤0.3 m/s², steps 1.0–2.5, giving `STEP_THRESHOLD = 0.9` and 24 counted
for 20. Two things found on the way:

- **The at-rest "1g" is not 9.8, and it changes with tilt.** This GY-521 read
  ~0.9 m/s² off at rest, and ~1.1 after being tilted post-boot — eating half
  the old 1.8 threshold. `steps.cpp` now seeds the baseline at power-on and
  tracks it with a 2s moving average, so stillness reads ~0.08 at any angle.
- **Slow steps are two bumps** (heel strike, push-off). At 250–350ms debounce
  a 1 step/s walk counted ~1.45× high; 450ms merges them.

Mount the sensor where it will actually ride on the costume before tuning —
chest versus pocket moves these numbers a lot.

## Host-side tests

```bash
make -C tests
```

Compiles with a plain `c++` — no Arduino toolchain, no board attached — and
runs in CI on every PR (`.github/workflows/tests.yml`).

**What's tested, and why only this.** Only `hr_zones.cpp`: the BPM-to-zone and
zone-to-pixel math. That's the one part of this firmware that can be wrong
without *looking* wrong — a bar of the wrong length is still a plausible bar.
Everything else is I/O against hardware that can't be exercised off-device,
and mocking the HUB75 driver would test the mocks. Worth noting that every bug
actually hit on this project so far (unreadable glyph, wrong clock phase,
single-buffer tearing, the heart popping instead of beating) was perceptual or
electrical and was found by looking at the panel — no test would have caught
any of them.

**They're property tests, not a table of expected values.** The thresholds in
`config.h` are wearer-specific and expected to be retuned; a test asserting
"110bpm is zone 3" would fail on every retune and train you to ignore it.
Instead they assert what must hold for *any* sane threshold set: slices tile
the panel exactly with no gap or overlap, the bar never shortens as BPM rises,
zone boundaries land exactly on slice edges, every zone is reachable, and the
ends clamp. So the workflow for a retune is: edit the constants, run the
tests, and find out immediately whether you broke the scale.

The suite has been mutation-checked — thresholds put out of order, the bar
re-anchored to 0bpm, an off-by-one in the tiling, and a removed clamp each
make it fail.

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

To watch serial, use the helper:

```
python3 tools/serial_monitor.py            # Ctrl-C to stop
python3 tools/serial_monitor.py /dev/cu.usbmodem201101 10   # or capture 10s
```

**Neither `arduino-cli monitor` nor `stty ... raw && cat` works on this
board** — both were tried and both print absolutely nothing while the sketch
is running normally, which is indistinguishable from a crashed sketch and will
send you debugging the wrong thing. (An earlier version of this README
recommended the `cat` form. It does not work.)

The reason is that the ESP32-S3's native USB-CDC only starts emitting once the
host asserts **DTR**, and neither of those tools raises it. `tools/serial_monitor.py`
opens the port and raises DTR first; that is the entire difference.

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

**This panel clocks data in on the NEGATIVE edge — set `mxconfig.clkphase =
false`.** The library defaults to the positive edge. With the default, the
whole image sat one pixel to the left, which is invisible everywhere except at
the wrap: the last column (x=63) showed the *next row's* first pixel instead
of its own. It presented as a few faint, unidentifiable dots in the
bottom-right corner where a solid red legend segment should have been. Worth
knowing because the symptom looks like dying LEDs or electrical noise, not
like a timing setting — if a single edge column ever looks wrong, suspect this
before suspecting the panel.

**Flicker had two causes, both in `displaySetup()`.** Redrawing the whole frame
each loop into a single buffer meant the panel scanned out half-drawn frames;
`mxconfig.double_buff = true` plus a `flipDMABuffer()` at the end of each draw
fixes the tearing. Separately, the library's 8MHz/60Hz defaults beat visibly
against both eyes and camera shutters — now `HZ_16M` with `min_refresh_rate =
120`. If ghosting (faint duplicate rows) ever appears, that 16MHz is the first
thing to back off, then `setLatBlanking(2)`.

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
