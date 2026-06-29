# firmware/epd397 — Waveshare ESP32-S3-ePaper-3.97 dashboard

Date: 2026-06-29. Target board confirmed in hand: ESP32-S3 rev v0.2, 8 MB octal
PSRAM, 16 MB QIO flash, USB-Serial/JTAG, MAC `9c:13:9e:a7:32:f8`, on
`/dev/cu.usbmodem83401`.

## Goal

A new firmware target for the **Waveshare ESP32-S3-ePaper-3.97** (800×480 active-
matrix e-paper). Same dual-mode product as `firmware/rlcd`: boot into a hand-drawn
**climate dashboard** (temp/humidity from the onboard SHTC3 + RTC clock + battery);
**KEY** toggles to the shared **Axiom page-deck** (overview/github/todo/calendar/
codex/claude). Rendered in **4-level grayscale** so antialiased Latin text keeps
its gray edges instead of jagged 1 bpp.

## Why clone rlcd

`firmware/rlcd` is the closest in-tree relative: same SoC family (ESP32-S3 +
PSRAM), same onboard sensor set (SHTC3 0x70, PCF85063 RTC 0x51), and it already
implements the exact dual-mode dashboard+deck with the "compose into a 16-bit
`TFT_eSprite`, then pack at push time" pipeline. The deltas vs rlcd are the panel
(e-paper not ST7305 LCD), resolution (800×480 not 400×300), color depth (4-gray
not 1 bpp), and battery source (AXP2101 PMIC not ADC).

## Reused verbatim from rlcd

`proto/` (parser·schema·envelope), `net/` (wifi·axiom), `store/nvs`,
`power/refresh_schedule`, `sense/shtc3` (−4 °C self-heat offset), `sense/rtc`
(PCF85063), `render/engine`, `render/primitives`, `render/palette`. The Axiom
page-deck contract is identical, so no producer changes.

## New / changed modules

- **`display/epd397.{h,cpp}`** — e-paper driver ported from Waveshare's
  `EPD_3in97` (SSD1677-family: `0x12` SWRESET, `0x24/0x26` RAM planes,
  `0x22/0x20` turn-on, `0x11` data-entry, `0x44/0x45` windows; **BUSY active-
  HIGH** GPIO3). Hardware SPI (HSPI) ~12 MHz; SCK11/MOSI12/CS10/DC9/RST46. Boot
  runs the full init + white `Clear` to wipe factory ghosting. Each content push
  re-runs `Init_4GRAY` + `Display_4Gray` (demo-proven; cheap vs the ~3 s
  waveform). Driver owns the 96000-byte gray buffer; `epd397_push(px,w,h)` packs
  then displays; `epd397_last_frame()` exposes it for the screenshot oracle.
- **`render/gray_pack.{h,cpp}`** (replaces `mono_pack`) — quantize the 16-bit
  RGB565 sprite to the panel's 2bpp buffer: row-major, 4 px/byte MSB-first,
  code 0=black…3=white by luminance (`lum = (77R+150G+29B)>>8`,
  `code = (lum*3+127)/255`). Plus `gray_plane_byte()` — splits 8 pixels' codes
  into one SSD1677 plane byte (plane 0x24 set for codes {0,2}; plane 0x26 set
  for codes {0,1}). Both pure + native-unit-tested.
- **`render/fonts.cpp`** — retarget the ladder to 800×480: add a 2× rung (24 pt
  @ setTextSize 2 ≈ 80 px) for detail-page heroes, nominal heights H1=80 /
  H2=30 / Body=30 / Sm=16 (the HIL-proven c3-x4 800×480 values). Latin ≤24 pt
  stays setTextSize 1 (keeps AA); only the 80 px rung scales (blocky but big).
- **`render/page.{h,cpp}`** — `SCREEN_W=800 SCREEN_H=480 SIDEBAR_W=110`; swap
  `st7305_*`→`epd397_*`; drop the LCD dark-mode toggle (e-paper is read
  black-on-white); retarget the hand-drawn dashboard coordinates; add
  width-fit (`content_right` vs `CONTENT_W`) alongside the existing height-fit
  so a wide masthead scales down instead of clipping the sidebar; bump
  `kSpacingCompact` to 0.70.
- **`sense/battery.cpp`** — read % + mV from AXP2101 (XPowersLib `XPowersPMU`,
  0x34) with `enableBattDetection`/`enableBattVoltageMeasure`/`fuelGaugeControl`.
  Rails are left untouched — the vendor Arduino panel demo drives the display
  with no PMIC init, so the panel rail is on by default; AXP is battery-only.
- **`main.cpp`** — I2C on **SDA41/SCL42**; AXP init; e-paper refresh cadence;
  console `p` dumps the gray buffer.

## Refresh cadence (e-paper can't flash every 30 s)

- Mode switch / page change / network refresh → full 4-gray refresh (~3 s).
- Dashboard tick re-renders only when the displayed **minute** changes (not the
  rlcd 30 s tick), so ≈once/min.
- Boot does a full-white clear to fight ghosting.
- Deferred: the panel's 1.5 s fast-mono and 0.3 s partial paths (add if the
  4-gray cadence feels heavy in HIL).

## Memory

8 MB PSRAM → 16-bit sprite (750 KB) + 96 KB gray buffer in PSRAM with room to
spare. No c3-x4-style fetch-and-restart needed (that was a no-PSRAM workaround).

## HIL tooling

- `tools/serial_probe.py` (kept) — pyserial console with dtr/rts=False to dodge
  the S3 USB-Serial/JTAG download trap; `--send "t,m,s"`.
- `tools/capture_frames.py` (rewritten for 4-gray) — drive console `p`, decode
  the 96000-byte 2bpp dump → 800×480 PNG (4 gray levels) as a device-exact
  oracle.
- Build/flash: `pio run -e waveshare-s3-epd397-dev -t upload --upload-port
  /dev/cu.usbmodem83401`. Native logic tests: `pio test -e native`.

## Verification plan

1. `pio test -e native` green (palette, parser, engine, fonts, refresh,
   envelope, **gray_pack**).
2. Build the device env clean.
3. Flash; serial shows boot + sensor + AXP + display-ready logs.
4. Bring-up: confirm a white clear, then a test glyph renders upright (validates
   gray packing + orientation) via the `p`→PNG oracle.
5. Dashboard + each deck page captured via the oracle; tune coordinates/fonts.
