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
full 4-gray refresh (~3 s). A cold boot does one white clear to wipe ghosting.
Deep-sleep wakes preserve the old image through controller initialization and
perform exactly one visible update, with no intermediate blank/status frames.
The default `waveshare-s3-epd397-dev` environment stays awake for console work.
The production `waveshare-s3-epd397` environment renders once, puts the panel in
deep sleep, wakes on the next minute boundary, and also wakes from the wheel's
GPIO4/GPIO5/GPIO6 inputs. Default remains dev until the production timer/button
wake cycle passes on the exact target.

## Build + Flash

Create `include/config.h` from `include/config.h.example` and fill Wi-Fi/Axiom
plus `LOCAL_TZ`. USB suffixes drift and another ESP32-S3 may be attached; keep a
port only after `read_mac` matches this unit's `9C:13:9E:A7:32:F8`:

```bash
EPD397_PORT=/dev/cu.usbmodemNNNNN
uvx --from 'esptool==4.9.0' esptool.py \
  --chip esp32s3 --port "$EPD397_PORT" \
  --before usb_reset --after watchdog_reset read_mac
./scripts/pio-run.sh epd397 run \
  -d firmware/epd397 -e waveshare-s3-epd397-dev
./scripts/pio-run.sh epd397 run \
  -d firmware/epd397 -e waveshare-s3-epd397-dev \
  -t upload --upload-port "$EPD397_PORT"
```

Use `waveshare-s3-epd397` only for production sleep testing:

```bash
./scripts/pio-run.sh epd397 run \
  -d firmware/epd397 -e waveshare-s3-epd397
./scripts/pio-run.sh epd397 run \
  -d firmware/epd397 -e waveshare-s3-epd397 \
  -t upload --upload-port "$EPD397_PORT"
```

Native logic tests (palette, parser, layout engine, 4-gray packing):

```bash
./scripts/pio-run.sh epd397 test -d firmware/epd397 -e native
```

Monitor without DTR/RTS toggling the S3 into download mode:

```bash
uv run firmware/epd397/tools/console.py \
  --port "$EPD397_PORT" --seconds 30
```

Production deep sleep removes the port; reopen the raw console after each
re-enumeration. Do not use the pyserial-based probe or screenshot capture
against the production sleep build.

## Buttons

The 3-way **wheel** (Up/Function/Down) drives the whole UI. **BOOT** and the
hardware **PWR** key are left free, so PWR powers the board off cleanly.

| Gesture | Dashboard | Deck |
|---|---|---|
| **FN** (GPIO5) tap | → deck mode | → dashboard mode |
| **UP** (GPIO4) tap | re-read sensors | previous page |
| **DOWN** (GPIO6) tap | re-read sensors | next page |
| any, hold ≥800 ms | network refresh (NTP/RTC + Axiom) | same |

All refreshes use the panel's 4-gray waveform (~3 s, hardware-fixed — there is
no fast 4-gray mode in the controller). Holding the wheel triggers a network
refresh.
In the production sleep build, only the wheel GPIOs (4/5/6) are armed as EXT1
wake sources; GPIO0/BOOT is intentionally excluded.

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

For dev-console automation, use the raw console, e.g.
`uv run firmware/epd397/tools/console.py --port "$EPD397_PORT" --send "t,m,s" --seconds 20`.

## Exact screenshots

`tools/capture_frames.py` drives the firmware console, captures the current
4-gray framebuffer with `p`, decodes the panel-native 2bpp bytes, and writes
800×480 PNGs (4 gray levels) that match the device exactly:

```bash
uv run firmware/epd397/tools/capture_frames.py \
  --port "$EPD397_PORT" --open
```

Output:

- `native/*.png` — exact 800×480 panel frames
- `contact-sheet.png` — dashboard + all cached deck pages

Capture the current cached deck without a network refresh:

```bash
uv run firmware/epd397/tools/capture_frames.py \
  --port "$EPD397_PORT" --no-refresh --open
```
