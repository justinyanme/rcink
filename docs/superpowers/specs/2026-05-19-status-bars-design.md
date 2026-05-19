---
title: Top status bar + slim bottom nav
date: 2026-05-19
status: proposed
---

# Goal

Tighten the dashboard chrome so on-screen real estate is dominated by page
content, not by repeated indicators. Surface device-side telemetry (battery,
temp, humidity) that today is invisible.

Three concrete asks from the user:

1. The bottom `data` / `refresh` text feels heavy; merge with other chrome and
   use icons instead of words. Page name + brand-icon row at the foot of
   every page is redundant with the firmware-painted indicator.
2. The device has an SHT40 temp/humidity sensor and a 1250 mAh Li-ion
   battery via M5PM1; neither is shown. Put them at the top, with the
   battery rendered as a battery icon (not raw `%`).
3. Confirm Claude / Codex brand colors are sensible inside the 6-color
   palette (the E6 panel only does black / white / red / yellow / green / blue).

# Non-goals

- Adding a wall-clock to the chrome (data-time + refresh-time already cover
  "how fresh is this view").
- Reading battery / env sensors on the producer side (it's a Mac; the
  device data is what matters).
- Reworking individual page bodies. Only the per-page footer row goes.
- New layout primitives in the producer DSL. Status bars are firmware-only
  overlays.

# Decisions locked during brainstorming

| Question | Answer |
|---|---|
| Where do battery / env / data / refresh times live? | Top status bar (one line), bottom strip stays only for nav |
| How dense is the top bar? | Full: battery, temp, humidity, data time, refresh time, with two `·` dividers grouping hardware vs freshness |
| Per-page footer row (brand icons + `rcpaper · X`) | Delete from every page; firmware nav strip shows current page name |
| Claude / Codex colors | Keep `red` / `green` — confirmed |
| Page header vs new top bar | Firmware reserves viewport top, same pattern as bottom strip — pages don't change |
| Sensor read failure | Render `—` in place of the number, keep icon visible |

# Architecture

```
┌───────────────────────────────────────────────────┐   ← TOP_STRIP_H ≈ 22
│ [batt] 75% · 🌡 24°C  💧 58% · 📥 14:32  🔄 14:45 │      firmware overlay
├───────────────────────────────────────────────────┤
│                                                   │
│                                                   │
│              page content viewport                │      producer layout
│       y0 = TOP_STRIP_H, h = H − TOP − BOT         │
│                                                   │
│                                                   │
├───────────────────────────────────────────────────┤   ← BOT_STRIP_H ≈ 22
│ ◀ CODEX     ●○○○○ OVERVIEW     CAL ▶              │      firmware overlay
└───────────────────────────────────────────────────┘
```

`paint_status_bar()` (top) and `paint_indicator()` (bottom, slimmed) are
both drawn after the page layout, so the page can't accidentally write into
them. The page viewport is reduced top + bottom; existing layouts continue
to work because they already expect a reduced height.

## Sizes

- `TOP_STRIP_H = 22` — single row of `FreeSansBold9pt7b` text + 14px icons.
- `BOT_STRIP_H = 22` — same.
- Net usable page height: `600 − 22 − 22 = 556` (was `600 − 36 = 564`; loses
  8 px, but each page also reclaims ~24 px from the deleted footer row).

# Top status bar

Layout left → right:

```
[battery glyph] 75%   ·   [thermometer] 24°C   [droplet] 58%   ·   [download] 14:32   [refresh] 14:45
```

- **Battery glyph**: drawn geometrically, no PNG asset.
  - 22×10 outline + 2×4 nipple, fill bar 0..18 px wide scaled to percent.
  - Fill color: `green` ≥ 30%, `yellow` ≥ 10%, `red` < 10%.
- **Temperature**: icon `thermometer` (Lucide), `24°C` integer °C.
- **Humidity**: icon `droplet` (Lucide), `58%` integer.
- **Data time**: icon `download` (Lucide), `HH:MM` from the current page's
  `data_ts` in local TZ.
- **Refresh time**: icon `refresh-cw` (Lucide), `HH:MM` from
  `DeviceState::last_fetch_s` in local TZ.
- Two `·` dividers (plain `setCursor` glyphs) group hardware vs freshness.
- On sensor read failure: number renders as `—`. Icon stays.
- Bottom hairline (`drawFastHLine`) separates the strip from page content.

# Bottom nav strip

Single row, 22 px tall:

```
 ◀ CODEX     ●○○○○ OVERVIEW     CAL ▶
```

- Prev arrow + PREV_ID label (left-anchored, 8 px pad).
- Next ID label + next arrow (right-anchored, 8 px pad).
- Center: pagination dots followed by current page name.
- Dots vertically centered, current is filled, others outlined (unchanged).
- Current page name renders right of the dot row with 8 px gap.
- Hairline rule at the top edge of the strip.
- No timestamps here anymore.

# Sensor reads (firmware)

New module `src/sense/env.{h,cpp}`:

```cpp
struct EnvReading {
  float temp_c;
  float humidity_pct;
  bool ok;
};
EnvReading read_sht40();   // single-shot, ~15 ms blocking, retries 3x
```

- Uses `M5.In_I2C` (M5Unified abstraction), address `0x44`.
- Cmd `0xFD` (high precision, no clock stretching).
- Wait 10 ms, read 6 bytes: `t_msb t_lsb t_crc rh_msb rh_lsb rh_crc`.
- Validate both CRCs (polynomial `0x31`, init `0xFF`). On CRC fail → retry.
- Convert: `t_c = -45 + 175 * raw_t/65535`, `rh_pct = 100 * raw_h/65535`.

New module `src/sense/battery.{h,cpp}`:

```cpp
struct BatteryReading {
  uint8_t pct;          // 0..100
  uint16_t mv;
  bool ok;
};
BatteryReading read_battery();
```

- Calls `M5PM1::readVbat(&mv)` once.
- Maps mV → pct via a 9-point Li-ion LUT (3000/3300/3500/3650/3750/3850/3950/4050/4200 mV ↔ 0/5/15/30/45/60/75/90/100 %). Clamp to [0,100].

Both readings stored on `DeviceState` so `paint_status_bar` reads from
state, not directly from hardware.

## Where to call them

- Read once in `setup()` after `M5.begin()`, before the first render.
- Re-read after every `refetch_now()` (so a forced refresh updates env too,
  not just data).
- Dev console: optional `e` command (env dump) for diagnostics — out of
  scope, defer.

# Icon assets

Drop four new SVGs into `tools/icons/src/` and add aliases in `bake.py`:

| DSL name | Lucide SVG | Used for |
|---|---|---|
| `thermometer` | `thermometer.svg` | Top bar temp |
| `droplet` | `droplet.svg` | Top bar humidity |
| `download` | `download.svg` | Top bar data time |
| `refresh` | `refresh-cw.svg` | Top bar refresh time (DSL name kept simple) |

Battery does not become an icon — it's geometric so the fill animates the
percentage visually.

Run `uv run --with pillow python tools/icons/bake.py` once after dropping
the SVGs; commit both the SVGs and the regenerated `icons_data.h`.

# Producer changes

Delete the trailing brand-icon-row + `text("rcpaper · …")` footer from each
of:

- `producer/rcpaper/pages/overview.py` (the final `hstack` inside the page
  `vstack` — currently ~lines 195–206)
- `producer/rcpaper/pages/claude.py` (footer hstack near the end)
- `producer/rcpaper/pages/codex.py` (footer hstack near the end)
- `producer/rcpaper/pages/calendar.py` (footer hstack near the end)
- `producer/rcpaper/pages/todo.py` (footer hstack near the end)

The trailing `divider` + flex spacer above each footer goes too (they only
existed to anchor the footer at the bottom). Page content now ends with
its last data row; the firmware draws the nav strip below.

The `image.py` page keeps its caption footer — it's content, not chrome.

# Failure modes

| Condition | Behavior |
|---|---|
| SHT40 not present / NACK | Both temp and humidity show `—`. Icons remain. No retry storm — single I2C transaction with 3 in-place retries on CRC fail, then give up. |
| PM1 `readVbat` returns error | Battery glyph still drawn, fill = 0, text = `—`. |
| `data_ts == 0` (page has no timestamp) | `📥 --:--` (unchanged from today's behavior). |
| First-ever boot, no fetch yet | `🔄 --:--`. |
| Nav: only one page | Skip dots, skip prev/next labels, just show current name centered. |

# Out of scope

- Wall-clock in chrome.
- Persisting battery / env to NVS or pushing to Axiom.
- Animating the battery glyph.
- Adapting top-bar layout to localized 14°F / different units.
- `image.py` page chrome adjustments.

# Validation

1. Native tests still pass (`pio test -e native`) — pure-logic paths
   unchanged.
2. Build firmware (`pio run -e m5stack-papercolor`) and flash; visually
   verify on device:
   - All 7 pages (overview / claude / codex / calendar / todo / quote /
     image) render with top + bottom strips, no overlap, no clipping.
   - Battery fill ≈ matches device power level after a long unplugged run.
   - Temp/humid reads sane indoor values (18–28 °C, 30–80 %).
   - Holding finger near the SHT40 footprint should bump humidity reading
     within one refresh cycle.
3. Producer HTML preview (`producer/scripts/preview.py` or equivalent)
   still renders pages — the firmware-only overlays just won't appear in
   the browser preview, which is fine.

# References

- Existing indicator strip code: `src/main.cpp::paint_indicator` (~L92–162)
- M5PM1 battery API: `M5PM1::readVbat(uint16_t* mv)`
- SHT40 datasheet command set: `0xFD` (T+RH, high precision)
- Device pinout note: `~/Library/Mobile Documents/iCloud~md~obsidian/Documents/rocry/Notes/Hardware/_specific/M5Stack PaperColor.md`
