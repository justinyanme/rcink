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
Default remains dev until hardware-in-loop timer wake and latch-current checks
pass.

## Build + Flash

Create `include/config.h` from `include/config.h.example` and fill Wi-Fi/Axiom
plus `LOCAL_TZ`.

```bash
pio run -e c3-x4-dev
pio run -e c3-x4-dev -t upload --upload-port /dev/cu.usbmodem101
```

Use `c3-x4` only for production sleep testing:

```bash
pio run -e c3-x4
pio run -e c3-x4 -t upload --upload-port /dev/cu.usbmodem101
```

Native logic tests:

```bash
pio test -e native
```

The target uses the pioarduino ESP32-C3 platform pinned in `platformio.ini`.
Monitor without DTR/RTS toggling the USB-CDC boot state:

```bash
pio device monitor -p /dev/cu.usbmodem101 -b 115200
```

For non-interactive console work:

```bash
uv run tools/x4_console.py --port /dev/cu.usbmodem101 --send s --read 10
```

## Buttons

| Gesture | Action |
|---|---|
| LEFT or UP | previous page |
| RIGHT or DOWN | next page |
| CONFIRM | fetch from Axiom; on success save NVS and restart |
| BACK | re-render current cached page |
| POWER | wakes the production deep-sleep build |

The six UI buttons are ADC-ladder buttons, so they cannot wake the ESP32-C3
from deep sleep. Production sleep arms the separate GPIO3 power button.

## Console

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
uv run tools/x4_console.py --port /dev/cu.usbmodem101 --send P --read 14 | uv run tools/fb_to_png.py /tmp/x4.png
```

`tools/preview_sidebar.py` is a narrow layout helper for sidebar iteration; it
is not a full firmware render oracle.
