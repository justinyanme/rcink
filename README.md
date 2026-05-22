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

1. Copy `src/config.h.example` to `src/config.h`. Fill in WiFi credentials,
   Axiom token, and the Axiom Root CA PEM.
2. Make sure the producer has written at least one event to the
   `rcpapercolor` Axiom dataset (run `cd producer && uv run claude-fetch`).
3. Build + flash:

   ```bash
   pio run -e m5stack-papercolor -t upload
   pio device monitor -p /dev/cu.usbmodem83401 -b 115200 --rts 0 --dtr 0
   ```

4. Device boot sequence (first time): WiFi → NTP → Axiom query → render →
   deep sleep. Subsequent wakes are timer- or button-driven.

If serial stays silent after upload, avoid repeated default monitor opens.
ESP32-S3 USB-CDC can be left in DOWNLOAD mode by DTR/RTS toggles. Power-cycle
PaperColor with double-tap PWR, then single-tap PWR.

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

Rapid taps within ~500 ms of each other coalesce into a single net page
jump and one e-ink refresh. Example: BtnA × 3 then BtnB × 1 quickly →
device advances 2 pages with one render (≈ 17 s) instead of four.
The window slides on each new tap, caps at 2 s total, and a BtnC press
during the window cancels nav and triggers a refetch instead. Each press
flashes the LEDs in its color so you know it registered.

## Native tests

```bash
pio test -e native
```

Covers: colour palette, JSON parser, layout engine. Rendering, WiFi, NVS, and
sleep are device-bound and verified on hardware as part of the implementation
plan (`docs/superpowers/plans/2026-05-17-rcpapercolor-firmware.md`).

## Design + plans

- Spec: `docs/superpowers/specs/2026-05-17-rcpapercolor-design.md`
- Producer plan: `docs/superpowers/plans/2026-05-17-rcpapercolor-producer.md`
- Firmware plan: `docs/superpowers/plans/2026-05-17-rcpapercolor-firmware.md`
