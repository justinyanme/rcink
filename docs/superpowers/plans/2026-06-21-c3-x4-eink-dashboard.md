# Plan — firmware/c3-x4: Xteink X4 e-ink dashboard

Status: **v1 WORKING & HIL-verified 2026-06-21** (not committed — user didn't ask).

## What shipped

A working Wi-Fi/Axiom e-ink dashboard on the real Xteink X4. Every path was
HIL-verified on `/dev/cu.usbmodem101`: SSD1677 bring-up; cold-boot first run
(empty cache → fetch → reboot → render); cache-boot render; manual/auto refresh;
the 6-page deck (overview/github/todo/calendar/codex/claude) renders at 800×480
B&W with correct CJK. Render output confirmed via the frame-capture → PNG oracle.
Native tests (6 suites) green. Build: 1.6MB, RAM 28% static + 48KB runtime sprite.

**Platform pivot:** the stock `espressif32 @ 6.12.0` (Arduino 2.x) pins a RISC-V
toolchain (8.4.0) that won't download on this machine. Switched to the
**pioarduino fork `55.03.37`** (Arduino 3.3.7 / IDF 5.5, toolchain 14.2.0
already cached) — the same platform crosspoint-reader uses. TFT_eSPI 2.5.43
compiles clean on it (sprite-only; benign "DMA not supported on C3" warning).

**Three hard problems & their fixes (all driven by the C3 having no PSRAM):**
1. *Compose buffer* — a 16-bit 800×480 sprite is 750KB. Use a **1-bit
   TFT_eSprite (48KB)**; its buffer layout (100 B/line, MSB-first, bit=1=white)
   is byte-identical to the SSD1677 framebuffer, so `push_canvas()` is a
   `memcpy`. B&W palette collapses every non-white ink to black.
2. *Fetch memory* — the Axiom response is ~38KB and the C3 can't hold the 48KB
   sprite + ~40KB TLS + the parse at once (getString OOMs, streaming hits
   NoMemory, re-allocating the sprite after a fetch fails on fragmentation). Fix:
   **free the sprite for the fetch (getString fits) → save to NVS → reboot to
   render from the clean cache** (`fetch_and_restart`). Reboot is the reliable
   way to reclaim the 48KB contiguous block; cache-boot render is fast.
3. *lwIP crash* — leaving SNTP polling its UDP socket during the Axiom DNS/TLS
   trips `assert udp_new_ip_type "Required to lock TCPIP core"` on IDF 5.x
   (reliably when the NTP sync timed out). Fix: `esp_sntp_stop()` after the sync
   attempt in `sync_ntp_once`.

**HIL tooling:** `tools/x4_console.py` (pyserial, DTR/RTS held low to avoid the
USB-CDC download-trap; `pio device monitor` needs a TTY and fails in an agent
shell). Frame oracle: console `P` dumps the 1-bit buffer as hex → `tools/
fb_to_png.py` → PNG. Builds/flashes need the network sandbox disabled.

**Known follow-ups:** buttons (ADC ladder, GPIO1/2 + power GPIO3, thresholds
from crosspoint InputManager) are coded but need a physical press to verify;
content is top-anchored (the shared deck is portrait-oriented) — a landscape
x4 page set is a producer-side improvement; deep sleep + battery-latch (GPIO13)
deferred (v1 runs on USB). Auto-refresh reboots every 30 min by design.

---

Status: in progress (2026-06-21). Target reserved in README as "v1 work in progress".

## Goal

A Wi-Fi/Axiom e-ink dashboard for the **Xteink X4** (ESP32-C3 + SSD1677 /
GDEQ0426T82, 4.26" **800×480 B&W**), mirroring the existing rcink dashboards
(`s3`, `t-amoled`): pull the shared Axiom page-deck (`type == rcpaper_page`),
render via the shared layout engine, page through with buttons, refresh on a
30-min schedule. The producer already ships a `c3-x4` device profile
(`devices_profile.py`: width=800, height=480, B&W), so **only firmware is new**.

## Hardware (from crosspoint-reader open-x4-sdk)

- **SoC**: ESP32-C3, RISC-V single core, **no PSRAM**, ~380KB usable RAM, 16MB flash.
- **Panel**: SSD1677, 800×480, 1bpp B&W framebuffer = 48000 B (100 B/line, 0xFF=white).
- **E-ink SPI**: SCLK=8, MOSI=10, CS=21, DC=4, RST=5, BUSY=6 (active HIGH), MISO=7.
- **Battery**: ADC GPIO0 (2.0× divider). **USB detect**: GPIO20. **Power latch**: GPIO13.
- **Buttons**: 6 on two ADC ladders (GPIO1/GPIO2) + power button GPIO3.

## Key architectural decision — the C3 memory wall

The shared render stack composes a page into an off-screen `TFT_eSprite`. A
16-bit 800×480 sprite is **750KB** — impossible on the C3. So this target
composes into a **1-bit sprite (48KB)** and packs it into the SSD1677
framebuffer. B&W is the native design target, so 1-bit loses nothing.

The panel is driven by the **vendored `EInkDisplay` library** (`lib/EInkDisplay`,
from crosspoint-reader, MIT-ish, attributed) rather than a hand-rolled driver:
SSD1677 init / LUT / full+fast refresh are tricky and this code is HW-proven on
this exact device. Single-buffer mode (`-DEINK_DISPLAY_SINGLE_BUFFER_MODE`) keeps
the controller's RED RAM as the previous-frame store — 48KB total on the MCU.

Net RAM during render: 48KB compose sprite + 48KB EInkDisplay framebuffer = 96KB.
Wi-Fi/TLS is disconnected before render (as in t-amoled), so they don't overlap.

## Reuse map (ported verbatim from t-amoled)

`render/{engine,fonts,primitives,palette}`, `proto/*`, `net/{wifi,axiom}`,
`power/refresh_schedule`, `sense/battery`, `store/nvs`, most of `main.cpp`.

Rewritten for this device: `display/pins_config.h`, the display bridge in
`render/page.cpp` (`display_init`/`push_canvas`/`clear_screen`), `render/page.h`
geometry (800×480), `render/palette.cpp` (B&W), buttons in `main.cpp`.

## Build / flash

```
pio run -e c3-x4-bringup -t upload --upload-port /dev/cu.usbmodem101   # driver test pattern
pio run -e c3-x4-dev     -t upload --upload-port /dev/cu.usbmodem101   # full firmware
pio test -e native                                                    # pure-logic tests
```

## Phases

1. **Scaffold + dev setup** — copy t-amoled, vendor EInkDisplay, platformio.ini
   (C3), 16MB partitions, config.h (from studio), X4 pins, `bringup.cpp`.
2. **SSD1677 bring-up** — flash `bringup.cpp` test pattern, confirm panel (full +
   fast refresh, orientation, polarity).
3. **1-bit render bridge** — B&W palette, `setColorDepth(1)` sprite, pack →
   EInkDisplay framebuffer, frame-capture console cmd → PNG oracle, native test.
4. **main.cpp** — WiFi+NTP+Axiom deck, console, refresh schedule, buttons (ADC
   ladder), battery. Build green.
5. **HIL verify + iterate** — real fetch, render 800×480 deck, tune sidebar
   geometry / fit-to-height / refresh mode. Register x4 device in producer. Commit.

## Open questions / deferred

- Deep sleep between refreshes (e-ink holds image) + power latch (GPIO13) for
  battery operation — deferred; v1 runs always-on over USB.
- x4-specific page set vs shared deck — v1 shares the deck (same Axiom query).
