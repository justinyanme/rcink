# rcink t-amoled

Firmware target for the LILYGO T-Display-S3 AMOLED 1.91" board:

- ESP32-S3R8, 16 MB flash, 8 MB PSRAM
- RM67162 AMOLED, native 240×536, rendered in **landscape 536×240** (`lcd_setRotation(1)`)
- Two buttons: GPIO0 and GPIO21
- USB-CDC console on `/dev/cu.usbmodem*`

This target mirrors the PaperColor Axiom flow: Wi-Fi -> Axiom latest page query
-> local NVS page cache -> button navigation -> render current `layout_json`.
The default `lilygo-t-amoled-dev` environment stays awake for USB console work
and uses `FETCH_INTERVAL_SEC` as its base auto-refetch cadence (default 1800).
The production `lilygo-t-amoled` environment renders once, disconnects Wi-Fi,
turns the AMOLED panel dark, and deep-sleeps until the next timer wake or
GPIO21 wake.
Default remains dev until the production timer/button wake cycle and dark-panel
behavior pass on the exact target.

CJK text renders via U8g2_for_TFT_eSPI + a baked WenQuanYi 16px GB2312 font
(`firmware/shared/rcink-tft/include/render/fonts/wqy16_gb2312.h`); Latin text keeps TFT_eSPI's built-in fonts.
Any string with a non-ASCII byte is drawn with the CJK font (see
`render/fonts.cpp`), mirroring firmware/papercolor's efontCN swap.

## Build + Flash

Create `include/config.h` from `include/config.h.example` and fill Wi-Fi/Axiom
values. USB suffixes drift and another ESP32-S3 may be attached; keep a port
only after `read_mac` matches this unit's `80:65:99:A0:4A:40`:

```bash
TAMOLED_PORT=/dev/cu.usbmodemNNNNN
uvx --from 'esptool==4.9.0' esptool.py \
  --chip esp32s3 --port "$TAMOLED_PORT" \
  --after watchdog_reset read_mac
./scripts/pio-run.sh t-amoled run \
  -d firmware/t-amoled -e lilygo-t-amoled-dev
./scripts/pio-run.sh t-amoled run \
  -d firmware/t-amoled -e lilygo-t-amoled-dev \
  -t upload --upload-port "$TAMOLED_PORT"
```

Use `lilygo-t-amoled` only for production sleep testing:

```bash
./scripts/pio-run.sh t-amoled run \
  -d firmware/t-amoled -e lilygo-t-amoled
./scripts/pio-run.sh t-amoled run \
  -d firmware/t-amoled -e lilygo-t-amoled \
  -t upload --upload-port "$TAMOLED_PORT"
```

For #3 HIL only, temporarily set the gitignored `include/config.h`
`FETCH_INTERVAL_SEC` to `120`. The actual interval is multiplied by 1/2/4 for
the persisted unchanged-fetch streak, then doubled below 20% battery; use
`[sleep] entering deep sleep for N s` rather than assuming every wake is two
minutes. After HIL, restore `1800`, clean/rebuild, and reflash production.
Restoring the file without reflashing leaves the device on the test cadence.

`esptool.cfg` contains the tested USB Serial/JTAG reset sequence for this
board. It should enter `DOWNLOAD(USB/UART0)` automatically during PlatformIO
upload on macOS. Production sleep removes the port. To recover a stable
download port, hold GPIO0/BOOT while reconnecting USB, then repeat the MAC
check before flashing.

Monitor without DTR/RTS toggling the S3 into download mode:

```bash
pio device monitor -p "$TAMOLED_PORT" -b 115200 --rts 0 --dtr 0
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

| Gesture | Awake/dev action | Production deep-sleep action |
|---|---|---|
| GPIO0 tap | next page | does not wake |
| GPIO21 tap | previous page | wakes, advances one page, shows it for 8 s, then sleeps |
| either, hold ≥800 ms | refetch from Axiom + redraw | not available while asleep |

GPIO0 doubles as BOOT — safe as a runtime button; it only enters download mode
when held across a reset. A short tap pages; a long hold refetches (the only
"third gesture" two buttons can offer without colliding with rapid paging).
In the production sleep build, only GPIO21 is armed as an EXT1 wake source;
GPIO0/BOOT is intentionally excluded. EXT1 wake is level-triggered, so
production keeps the page visible for eight seconds and requires GPIO21 to be
released steadily before re-arming it. A stuck 30-second press falls back to a
timer-only sleep, so holding it cannot skip repeatedly or leave the AMOLED on.

For hands-free console testing over USB (no DTR/RTS reset), see
`tools/serial_probe.py`, e.g.
`uv run tools/serial_probe.py --port "$TAMOLED_PORT" --send "n,b,f" --seconds 25`.
