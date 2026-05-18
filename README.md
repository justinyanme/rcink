# rcpapercolor

Battery-powered colour e-ink dashboard for the M5Stack PaperColor that renders
a generic JSON layout-tree fetched from Axiom. Producers run on macOS and push
one event per dashboard page; the device wakes every 15 min, fetches, renders,
and deep-sleeps.

Two halves of one repo:

- `producer/` — Python (uv) on Mac. Pulls Claude/Codex usage, pushes pages.
  Also supports one-off remote image pages via `uv run image-fetch <url>`.
  See `producer/README.md` for setup.
- `src/` + `platformio.ini` — ESP32-S3 firmware. PlatformIO + M5Unified +
  M5GFX. See below.

## Firmware quickstart

1. Copy `src/config.h.example` to `src/config.h`. Fill in WiFi credentials,
   Axiom token, and the Axiom Root CA PEM.
2. Make sure the producer has written at least one event to the
   `rcpapercolor` Axiom dataset (run `cd producer && uv run claude-fetch`).
3. Build + flash:

   ```bash
   pio run -e m5stack-papercolor -t upload
   pio device monitor
   ```

4. Device boot sequence (first time): WiFi → NTP → Axiom query → render →
   deep sleep. Subsequent wakes are timer- or button-driven.

## Buttons

- **BtnA** — next page
- **BtnB** — previous page
- **BtnC** — force re-fetch from Axiom now

## Native tests

```bash
pio test -e native
```

Covers: colour palette, JSON parser, layout engine. Rendering, WiFi, NVS, and
sleep are device-bound and verified on hardware as part of the implementation
plan (`docs/superpowers/plans/2026-05-17-rcpapercolor-firmware.md`).

## Design + plans

- Spec: `docs/superpowers/specs/2026-05-17-rcpapercolor-design.md`
- Producer plan: `docs/superpowers/plans/2026-05-17-rcpapercolor-producer.md`
- Firmware plan: `docs/superpowers/plans/2026-05-17-rcpapercolor-firmware.md`
