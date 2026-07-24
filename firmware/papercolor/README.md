# rcpapercolor

Battery-powered colour e-ink dashboard for the M5Stack PaperColor that renders
a generic JSON layout-tree fetched from Axiom. Producers run on macOS and push
one event per dashboard page; the device wakes every 30 min for scheduled
refresh, renders only changed content, and deep-sleeps. Page changes are
button-only.

Two halves of one repo:

- `producer/` — Python (uv) on Mac. Pulls Claude/Codex usage, pushes pages.
  Also supports one-off remote image pages via `uv run image-fetch <url>`.
  See `producer/README.md` for setup.
- `src/` + `platformio.ini` — ESP32-S3 firmware. PlatformIO + M5Unified +
  M5GFX. See below.

## Firmware quickstart

1. Copy `include/config.h.example` to `include/config.h`. Fill in WiFi credentials,
   Axiom token, and the Axiom Root CA PEM.
2. Make sure the producer has written at least one event to the
   `rcpapercolor` Axiom dataset (run `cd producer && uv run claude-fetch`).
3. Identify the exact target before flashing. USB suffixes drift and another
   ESP32-S3 may be attached; the PaperColor unit in hand has MAC
   `44:1B:F6:C1:31:90`. Set the port only after `esptool read_mac` matches:

   ```bash
   RCPAPER_PORT=/dev/cu.usbmodemNNNNN
   uvx --from 'esptool==4.9.0' esptool.py \
     --chip esp32s3 --port "$RCPAPER_PORT" \
     --after watchdog_reset read_mac
   ```

   Build + flash the production (deep-sleep) firmware — this is the default
   env, the one to run on battery. The shared PlatformIO config makes this
   upload exit with `--after watchdog-reset`:

   ```bash
   pio run -e m5stack-papercolor -t upload --upload-port "$RCPAPER_PORT"
   pio device monitor -p "$RCPAPER_PORT" -b 115200 --rts 0 --dtr 0
   ```

   Attach the monitor only immediately after an intentional flash/reset.
   Opening this unit's native USB-CDC port can produce
   `USB_UART_CHIP_RESET` even with RTS/DTR inactive, so attaching during a
   natural timer/button wake destroys that wake reason. Validate natural
   button wakes from the retained panel and USB disappearance instead.

   For cable-driven iteration use the dev env instead — it adds the USB-CDC
   command console and **disables deep sleep** (stays awake, does not refresh
   on the timer, drains the battery — never leave this on battery):

   ```bash
   pio run -e m5stack-papercolor-dev -t upload --upload-port "$RCPAPER_PORT"
   ```

4. Device boot sequence (first time): WiFi → NTP → Axiom query → render →
   deep sleep. Subsequent wakes are timer- or button-driven.

## USB wedge protocol

USB-Serial/JTAG tools that touch DTR/RTS can park the ESP32-S3 in ROM download
mode. A wedged PaperColor is silent, never sleeps, draws about 15–20 mA, and
stays wedged after USB unplug because the battery still powers it. This drains
the 1250 mAh battery in roughly three days; it caused the 2026-07-23 drain,
while the firmware sleep path was healthy.

For every serial or flash session:

1. Verify the port's MAC before attaching serial. If the port disappears or
   changes during a wake cycle, stop; never retry or race USB enumeration.
2. Pass `--after watchdog-reset` to every esptool operation so it exits through
   a watchdog reset.
3. Before unplugging for battery use, close host tools, wait for the wake cycle
   to finish, and confirm PaperColor's `/dev/cu.usbmodem…` node is absent. An
   enumerated but idle PaperColor port means the chip is wedged, not asleep.
   Power-cycle with double-tap PWR, then single-tap PWR; repeat the check and do
   not unplug until that port is absent.

## LED states

PaperColor RGB LEDs are low-duty heartbeats: brief pulse, off between pulses.

- white heartbeat — launch
- blue heartbeat — connecting WiFi
- green heartbeat — WiFi connected
- red heartbeat — WiFi failed
- cyan/blue heartbeat — NTP or Axiom sync
- amber/red heartbeat — sync/fetch failed, cache kept
- amber heartbeat — panel refresh/render
- short green flash — BtnA press registered (next-page)
- short blue flash — BtnB press registered (prev-page)
- short amber flash — BtnC press registered (refetch)
- idle states — LEDs off
- off — entering deep sleep

## Buttons

- **BtnA** — next page
- **BtnB** — previous page
- **BtnC** — force re-fetch from Axiom now
- **BtnA + BtnB** — diagnostics (battery, wake count, fetch/dedup state)

Repeated taps on the same navigation button within ~500 ms coalesce into a
single page jump and one e-ink refresh. Example: BtnA × 3 advances three pages
with one render (≈ 17 s) instead of three.
The window slides on each new tap, caps at 2 s total, and a BtnC press
during the window cancels nav and triggers a refetch instead. Each press
flashes the LEDs in its color so you know it registered.

A truly simultaneous A+B wake combines the ESP32 EXT1 wake bitmask with an
early snapshot of the live button levels. `EXT1 ANY_LOW` may latch only the
first human press; the other button can become low before runtime ISRs are
installed, leaving no falling edge to count. `setup()` therefore snapshots the
pins before `M5.begin()` while the user is still holding both, then unions that
mask with EXT1. Hold both buttons for about one second when opening
diagnostics. A staggered chord means holding the first button while pressing
the second. To reverse navigation direction, let the current wake cycle finish
before pressing the other button.

## Native tests

```bash
pio test -e native
```

Covers: colour palette, JSON parser, layout engine. Rendering, WiFi, NVS, and
sleep are device-bound and verified on hardware. The pre-merge hardware plan is
archived in [`../../docs/superpowers/ARCHIVE.md`](../../docs/superpowers/ARCHIVE.md).

## Design + plans

- Current umbrella spec: [`../../docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md`](../../docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md)
- Pre-merge rcpapercolor docs: [`../../docs/superpowers/ARCHIVE.md`](../../docs/superpowers/ARCHIVE.md)
