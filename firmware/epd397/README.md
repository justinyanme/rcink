# rcink epd397

Firmware target for the **Waveshare ESP32-S3-ePaper-3.97** board — a desk-climate
dashboard.

- ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM)
- **3.97" active-matrix e-paper**, driven landscape **800×480, 4-level grayscale**
- Onboard **SHTC3** temp/humidity (I2C 0x70), **PCF85063** RTC (I2C 0x51),
  **AXP2101** PMIC fuel gauge (I2C 0x34)
- Buttons: **BOOT** (GPIO0) and the wheel's **FUNCTION** centre (GPIO5). The
  physical **PWR** key is handled by the board power circuit, not by the app.
- USB-Serial/JTAG console on `/dev/cu.usbmodem*`

## What it shows

Boots into a hand-drawn **dashboard** redesigned for the 800×480 panel:

```
RCINK                                         14:32
DESK CLIMATE                            WED  JUN 29
──────────────────────────────────────────────────
                   TEMPERATURE
                    28.4 °C
HUMIDITY                                       64%
[████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]
──────────────────────────────────────────────────
BATTERY   [████████░░░]            4.01V       87%
```

The climate comes from the onboard SHTC3; the clock from the PCF85063 RTC
(NTP re-syncs it whenever Wi-Fi is up), so the dashboard is correct **offline**.
A second **mode** flips to the Axiom page-deck shared with `firmware/t-amoled`.

### How it draws

The render pipeline is reused wholesale from `firmware/rlcd`: the scene is
composed into a 16-bit `TFT_eSprite` (black ink on white). At push time the
sprite is quantised by luminance to the panel's native 4-gray 2bpp buffer
(`src/render/gray_pack.cpp`, `src/display/epd397.cpp`) — so antialiased Latin
edges survive as real grays rather than jagged 1 bpp. There is **no** AMOLED
white↔black swap; the e-paper is read black-on-white directly. TFT_eSPI is used
only as a sprite/font engine (its ILI9341 setup is never driven against real
hardware). CJK in deck mode renders via U8g2 + a baked WenQuanYi GB2312 font.

E-paper can't flash on every tick, so the dashboard re-renders only when the
displayed **minute** rolls over (≈once/min); mode switches and page changes do a
full 4-gray refresh (~3 s). Boot does a full white clear to wipe ghosting.

## Build + Flash

Create `src/config.h` from `src/config.h.example` and fill Wi-Fi/Axiom + `LOCAL_TZ`.

```bash
pio run -e waveshare-s3-epd397-dev
pio run -e waveshare-s3-epd397-dev -t upload --upload-port /dev/cu.usbmodem83401
```

Native logic tests (palette, parser, layout engine, 4-gray packing):

```bash
pio test -e native
```

Monitor without DTR/RTS toggling the S3 into download mode:

```bash
pio device monitor -p /dev/cu.usbmodem83401 -b 115200 --rts 0 --dtr 0
```

## Buttons

Only BOOT and FUNCTION generate firmware events. PWR is a hardware power key.

| Gesture | Dashboard | Deck |
|---|---|---|
| **FUNCTION** (GPIO5) tap | → deck mode | → dashboard mode |
| **BOOT** (GPIO0) tap | re-read sensors | next page (wraps) |
| either, hold ≥800 ms | network refresh (NTP/RTC + Axiom) | same |

BOOT doubles as the download strap — safe at runtime (it only enters download
mode when held across a reset).

## Console

- `f` network refresh (Wi-Fi → NTP/RTC + Axiom fetch)
- `r` re-render current mode
- `m` toggle dashboard ↔ deck
- `n` next page (deck only)
- `t` re-read sensors + redraw dashboard
- `s` state dump
- `q` dump current `layout_json`
- `p` dump the packed 4-gray framebuffer as hex for screenshot capture
- `c` clear screen
- `R` reboot

For hands-free console testing over USB (no DTR/RTS reset), see
`tools/serial_probe.py`, e.g. `uv run tools/serial_probe.py --send "t,m,s" --seconds 20`.

## Exact screenshots

`tools/capture_frames.py` drives the firmware console, captures the current
4-gray framebuffer with `p`, decodes the panel-native 2bpp bytes, and writes
800×480 PNGs (4 gray levels) that match the device exactly:

```bash
uv run tools/capture_frames.py --port /dev/cu.usbmodem83401 --open
```

Output:

- `native/*.png` — exact 800×480 panel frames
- `contact-sheet.png` — dashboard + all cached deck pages

Capture the current cached deck without a network refresh:

```bash
uv run tools/capture_frames.py --port /dev/cu.usbmodem83401 --no-refresh --open
```
