# rcink t-amoled

Firmware target for the LILYGO T-Display-S3 AMOLED 1.91" board:

- ESP32-S3R8, 16 MB flash, 8 MB PSRAM
- RM67162 AMOLED, native 240×536, rendered in **landscape 536×240** (`lcd_setRotation(1)`)
- Two buttons: GPIO0 and GPIO21
- USB-CDC console on `/dev/cu.usbmodem*`

This target mirrors the PaperColor Axiom flow: Wi-Fi → Axiom latest page query
→ local NVS page cache → button navigation → render current `layout_json`.

CJK text renders via U8g2_for_TFT_eSPI + a baked WenQuanYi 16px GB2312 font
(`src/render/fonts/wqy16_gb2312.h`); Latin text keeps TFT_eSPI's built-in fonts.
Any string with a non-ASCII byte is drawn with the CJK font (see
`render/fonts.cpp`), mirroring firmware/s3's efontCN swap.

## Build + Flash

Create `src/config.h` from `src/config.h.example` and fill Wi-Fi/Axiom values.

```bash
pio run -e lilygo-t-amoled-dev
pio run -e lilygo-t-amoled-dev -t upload --upload-port /dev/cu.usbmodem83401
```

`esptool.cfg` contains the tested USB Serial/JTAG reset sequence for this
board. It should enter `DOWNLOAD(USB/UART0)` automatically during PlatformIO
upload on macOS.

Monitor without DTR/RTS toggling the S3 into download mode:

```bash
pio device monitor -p /dev/cu.usbmodem83401 -b 115200 --rts 0 --dtr 0
```

## Console

- `f` fetch from Axiom and render
- `r` re-render current cached page
- `n` next page
- `b` previous page
- `s` state dump
- `q` dump current `layout_json`
- `c` clear screen
- `R` reboot

## Buttons

| Gesture | Action |
|---|---|
| GPIO0 tap | previous page |
| GPIO21 tap | next page |
| either, hold ≥800 ms | refetch from Axiom + redraw |

GPIO0 doubles as BOOT — safe as a runtime button; it only enters download mode
when held across a reset. A short tap pages; a long hold refetches (the only
"third gesture" two buttons can offer without colliding with rapid paging).

For hands-free console testing over USB (no DTR/RTS reset), see
`tools/serial_probe.py`, e.g. `uv run tools/serial_probe.py --send "n,b,f" --seconds 25`.
