# rcink c3-x4

Firmware target for the **Xteink X4** ESP32-C3 e-ink device.

- ESP32-C3, 16 MB flash, no PSRAM
- **GDEQ0426T82 / SSD1677 4.26" B&W e-ink**, driven landscape **800x480, 1 bpp**
- Battery ADC on GPIO0, power latch on GPIO13, USB detect on GPIO20
- Six buttons on two ADC ladders (GPIO1/GPIO2), plus active-low power button on
  GPIO3
- USB-Serial/JTAG console on `/dev/cu.usbmodem*`

## What it shows

This target renders the shared Axiom page deck from the Python producer:
Claude/Codex usage, calendar/todo style pages, GitHub activity, and any other
DSL pages written as `rcpaper_page` events. The right sidebar shows page
position, cache timestamp, stale state, and battery.

There is no local climate dashboard on this board. The X4 firmware is optimized
around a wide 800x480 cached page deck and the ESP32-C3 memory budget.

## How it draws

The render stack is shared with the other rcink targets: JSON DSL -> parsed
layout tree -> off-screen `TFT_eSprite` -> panel-native framebuffer. The X4 is
the exception in color depth: it composes into a **1-bit 800x480 sprite**
(48 KB) because the ESP32-C3 has no PSRAM.

The panel is driven by the vendored `EInkDisplay` library under `lib/`, proven
on this hardware by the crosspoint-reader firmware. TFT_eSPI is used only as a
sprite/font engine; no TFT panel is initialized.

The no-PSRAM constraint shapes the network path. A fetch frees the 48 KB sprite,
downloads the Axiom result, saves fresh pages to NVS, then restarts. The next
boot reloads NVS and renders from a clean heap. If the fetch fails, the firmware
rebuilds the sprite and keeps the existing cache.

The default `c3-x4-dev` environment stays awake for console work and scheduled
auto-refresh. The production `c3-x4` environment renders from cache, fetches
when due, keeps a 60-second button window, puts the SSD1677 to sleep, holds the
battery latch, and deep-sleeps until timer wake or the GPIO3 power button.
Default remains dev until the battery-powered timer/button wake and latch
survival checks pass on the exact target.

## Target identity — required

USB device paths are ephemeral, not identity. The required Xteink X4 is an
ESP32-C3 with base MAC `F8:5B:1B:5D:1D:60`. Set the current candidate port and
verify it before any flash or console access:

```bash
x4_port=/dev/cu.usbmodemNNN
"$HOME/.platformio-rcink-c3/penv/bin/esptool" \
  --chip esp32c3 --port "$x4_port" read-mac
```

Proceed only when the command reports ESP32-C3 and that exact MAC. Stop on a
mismatch; re-identify after any reset/upload that changes the port.

## Build + Flash

`firmware/c3-x4/include/config.h` is a local, gitignored secret input. Copy it
from `include/config.h.example`, fill Wi-Fi/Axiom/time-zone values, and never
commit its credentials. Run from the repository root and always name the
environment; default remains dev until battery HIL passes.

```bash
just native-c3-x4
./scripts/pio-run.sh c3-x4 run -d firmware/c3-x4 -e c3-x4-dev
./scripts/pio-run.sh c3-x4 run -d firmware/c3-x4 \
  -e c3-x4-dev -t upload --upload-port "$x4_port"
```

Use `c3-x4` only for production sleep testing:

```bash
./scripts/pio-run.sh c3-x4 run -d firmware/c3-x4 -e c3-x4
./scripts/pio-run.sh c3-x4 run -d firmware/c3-x4 \
  -e c3-x4 -t upload --upload-port "$x4_port"
```

For #4 HIL only, temporarily set `FETCH_INTERVAL_SEC` to `120`. Afterwards,
restore `1800`, clean/rebuild, and reflash production. Restoring the ignored
file without reflashing leaves the device on the test cadence.

The wrapper keeps this target's pinned pioarduino core isolated from the stock
ESP32-S3 targets.

Monitor without DTR/RTS toggling the USB-CDC boot state:

```bash
uv run firmware/c3-x4/tools/x4_console.py \
  --port "$x4_port" --read 75
```

Serial command dispatch is dev-only. Production serial is listen-only; during
its 60-second awake window, drive HIL with physical buttons, not `--send`.

## Production HIL timing and latch proof

`FETCH_INTERVAL_SEC` is a base, not guaranteed wake period. The effective
interval is multiplied by 1/2/4 for the persisted unchanged-fetch streak, then
doubled when a valid battery reading is below 20%. A temporary 120-second base
may therefore yield 120/240/480 seconds, or 240/480/960 seconds at low battery.
Use `[nvs] ... unch=...` and `[sleep] entering deep sleep for N s` as the oracle.

No current meter is required. With battery attached and USB disconnected, the
device must complete at least three consecutive sleep → timer wake → render
cycles without losing power; the image must remain visible while asleep.
GPIO3 POWER must then wake it, and an ADC-ladder navigation button must work in
the 60-second awake window. USB sleep cannot prove the GPIO13 latch because USB
masks latch failure; retained e-ink alone cannot prove the MCU is still powered.

Only after that proof: restore/reflash `FETCH_INTERVAL_SEC=1800`, change
`default_envs` from `c3-x4-dev` to `c3-x4`, rebuild, and commit the HIL evidence.

## Buttons

| Gesture | Action |
|---|---|
| LEFT or UP | previous page |
| RIGHT or DOWN | next page |
| CONFIRM | fetch from Axiom; on success save NVS and restart |
| BACK | show diagnostics |
| POWER | wakes the production deep-sleep build |

The six UI buttons are ADC-ladder buttons, so they cannot wake the ESP32-C3
from deep sleep. Production sleep arms the separate GPIO3 power button.

## Console

The commands below are available only in `c3-x4-dev`.

- `f` fetch from Axiom; on success save NVS and restart
- `r` re-render current cached page
- `n` next page
- `b` previous page
- `s` state dump
- `q` dump current `layout_json`
- `P` dump the 1 bpp framebuffer
- `c` clear screen
- `R` reboot
- `h` help

## Exact screenshots

Dump the current framebuffer and convert it on the Mac:

```bash
uv run firmware/c3-x4/tools/x4_console.py \
  --port "$x4_port" --send P --read 14 \
  | uv run firmware/c3-x4/tools/fb_to_png.py /tmp/x4.png
```

`tools/preview_sidebar.py` is a narrow layout helper for sidebar iteration; it
is not a full firmware render oracle.
