# rcink t-amoled

Firmware target for the LILYGO T-Display-S3 AMOLED 1.91" board:

- ESP32-S3R8, 16 MB flash, 8 MB PSRAM
- RM67162 AMOLED, native 240×536
- Two buttons: GPIO0 and GPIO21
- USB-CDC console on `/dev/cu.usbmodem*`

This target mirrors the PaperColor Axiom flow: Wi-Fi → Axiom latest page query
→ local NVS page cache → button navigation → render current `layout_json`.

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

GPIO21 maps to next page. GPIO0 maps to refetch; it is also BOOT, so avoid
holding it while resetting unless you intentionally want download mode.
