# rcink rlcd

Firmware target for the **Waveshare ESP32-S3-RLCD-4.2** board — a desk-climate
dashboard.

- ESP32-S3-WROOM-1-N16R8, 16 MB flash, 8 MB PSRAM
- **ST7305 4.2" monochrome reflective LCD** (e-ink-like), driven landscape
  **400×300, 1 bpp**
- Onboard **SHTC3** temp/humidity (I2C 0x70), **PCF85063** RTC (I2C 0x51),
  18650 battery ADC (GPIO4, 3:1 divider)
- Two side buttons: **BOOT** (GPIO0) and **KEY** (GPIO18)
- USB-CDC console on `/dev/cu.usbmodem*`

## What it shows

Boots into a hand-drawn **dashboard** modelled on Waveshare's reference UI,
redesigned for the 400×300 landscape panel:

```
RCINK                         14:32
DESK CLIMATE             WED  JUN 12
────────────────────────────────────
            TEMPERATURE
              28.4 °C
HUMIDITY                        64%
[██████████████░░░░░░░░░░░░░░░░░░░░]
────────────────────────────────────
BATTERY  [████████░░░]  4.01V    87%
```

The climate comes from the onboard SHTC3; the clock from the PCF85063 RTC
(NTP re-syncs it whenever Wi-Fi is up), so the dashboard is correct **offline**.
A second **mode** flips to the Axiom page-deck ported from `firmware/t-amoled`.

### How it draws

The render pipeline is reused wholesale from `firmware/t-amoled`: the scene is
composed into a 16-bit `TFT_eSprite` (black ink on white). At push time the
sprite is thresholded to 1 bpp and packed into the ST7305's native buffer
(`src/render/mono_pack.cpp`, `src/display/st7305.cpp`) — there is **no** AMOLED
white↔black swap; reflective panels are read black-on-white directly. TFT_eSPI
is used only as a sprite/font engine (its ILI9341 setup is never driven against
real hardware). CJK in deck mode renders via U8g2 + a baked WenQuanYi GB2312
font.

## Build + Flash

Create `src/config.h` from `src/config.h.example` and fill Wi-Fi/Axiom + `LOCAL_TZ`.

```bash
pio run -e waveshare-s3-rlcd-dev
pio run -e waveshare-s3-rlcd-dev -t upload --upload-port /dev/cu.usbmodem83401
```

Native logic tests (palette, parser, layout engine, ST7305 1 bpp packing):

```bash
pio test -e native
```

Monitor without DTR/RTS toggling the S3 into download mode:

```bash
pio device monitor -p /dev/cu.usbmodem83401 -b 115200 --rts 0 --dtr 0
```

## Buttons

| Gesture | Dashboard | Deck |
|---|---|---|
| **KEY** (GPIO18) tap | → deck mode | → dashboard mode |
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
- `c` clear screen
- `R` reboot

For hands-free console testing over USB (no DTR/RTS reset), see
`tools/serial_probe.py`, e.g. `uv run tools/serial_probe.py --send "t,m,s" --seconds 20`.

## Layout oracle

`tools/preview_dashboard.py` is a headless 400×300 PIL mock of the dashboard
(same black-on-white threshold as the firmware) for iterating layout without
flashing:

```bash
uv run tools/preview_dashboard.py && open /tmp/rlcd_dashboard.png
```
