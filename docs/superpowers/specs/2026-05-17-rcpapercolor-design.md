# rcpapercolor — design spec

**Date:** 2026-05-17
**Hardware target:** M5Stack PaperColor (ESP32-S3R8 + Spectra 6 4" 400×600 EPD)
**Status:** approved for implementation planning

## Goal

A battery-powered colour e-ink dashboard that renders Claude/Codex AI usage
(and future personal dashboards) as a rotating slideshow. The device is a
**generic JSON-to-UI renderer**; what's on screen is decided by data living in
Axiom, written by independent producer scripts. Adding a new dashboard kind
should require zero firmware changes — drop a new producer script, that's it.

## Non-goals (v1)

- Touch input / captive portal / OTA — config edited in `config.h` + reflashed.
- On-device OAuth or any provider-specific logic. Firmware is provider-agnostic.
- Animations, partial-region redraws, sub-second responsiveness. Spectra 6 EPD
  full redraw is 3–8 s; we accept that.
- Battery management beyond "wake → fetch/render → deep sleep". No charge
  curve, no low-battery dimming, no telemetry. Show battery % as text only.
- Multi-device fleet, audio, microSD, IMU, IR, RGB LED, ES8311/ES7210, RTC sync
  to NTP every wake. Hardware peripherals beyond EPD + buttons + WiFi + RTC
  wake are out of scope.

## Architecture

```
┌──────────────────────────┐    ingest     ┌─────────────────┐   APL POST     ┌────────────────────┐
│ Producers (Mac/launchd)  │ ─────────────►│ Axiom dataset   │◄──────────────│ Firmware (ESP32-S3)│
│  • claude_fetch.py       │  one event    │ "rcpapercolor"  │  every 15 min  │ wakes from sleep   │
│  • codex_fetch.py        │  per page     │ kind=page       │                │ render + sleep     │
│  • (future scripts)      │  every 15min  │ id=<provider>   │                └────────────────────┘
└──────────────────────────┘               │ layout=<tree>   │
                                           └─────────────────┘
```

Three pieces, three contracts:

1. **Producers ↔ Axiom**: HTTPS ingest of one `kind=page` event per script run.
2. **Axiom ↔ firmware**: HTTPS POST APL query, returns latest-per-id page list.
3. **Layout tree ↔ render engine**: a small declarative protocol (below).

Each producer is fully independent. Stale producers (no event in >1 h) auto-fall
out of the rotation set.

## Protocol — declarative layout tree

Every page is a tree of nodes. Each node is `{type, ...props, children?}`.
The firmware walks the tree, runs a flex-style layout pass, then a paint pass.

### Layout primitives

| `type`    | props                                                  |
|-----------|--------------------------------------------------------|
| `vstack`  | `gap`, `pad`, `align` (start\|center\|end), `children` |
| `hstack`  | `gap`, `pad`, `align`, `children`                      |
| `spacer`  | `size` (px; omit → flex)                               |
| `divider` | `color`, `thickness`                                   |

### Content primitives

| `type`      | props                                                                   |
|-------------|-------------------------------------------------------------------------|
| `text`      | `value`, `size` (h1\|h2\|body\|sm), `color`, `weight` (regular\|bold), `align`, `bg`, `pad`, `max_lines` |
| `hbar`      | `pct` (0–100), `color`, `height`, `label_left`, `label_right`           |
| `icon`      | `name` (registry key), `size`, `color`                                  |
| `image`     | `name` (PNG asset on flash), `width`, `height`                          |
| `sparkline` | `values` (number[]), `color`, `height`, `baseline` (auto\|0)            |
| `marker`    | `pct`, `color` — vertical tick on the previous sibling `hbar`           |

### Tokens

- **Sizes**: `h1`, `h2`, `body`, `sm` — concrete px set in firmware font registry.
- **Weights**: `regular`, `bold`.
- **Align**: `left`, `center`, `right`.
- **Colors** (Spectra 6 inks, the only addressable palette):
  `black`, `white`, `red`, `yellow`, `green`, `blue`.

No RGB, no grayscale, no opacity. The producer never invents colors.

### Layout pass

Flex-style. Parent's `type` decides main axis (`vstack`→vertical,
`hstack`→horizontal). Children sized by their content; `spacer` with no `size`
fills remaining space; `spacer` with `size` is a fixed gap. `align` controls
cross-axis alignment of children. `pad` insets the parent, `gap` separates
siblings on the main axis.

### Example

```json
{
  "type": "vstack", "pad": 14, "gap": 6,
  "children": [
    {"type":"text","value":"CLAUDE","size":"h1","color":"white","bg":"red","pad":8,"align":"center"},
    {"type":"hstack","gap":8,"align":"center","children":[
      {"type":"text","value":"42%","size":"h1","color":"blue"},
      {"type":"hbar","pct":42,"color":"blue","height":14}
    ]},
    {"type":"text","value":"5H · resets 2h 14m","size":"sm"},
    {"type":"sparkline","values":[12,18,14,22,19,28,31],"color":"blue","height":40},
    {"type":"spacer"},
    {"type":"text","value":"updated 14:32 · 1/2","size":"sm","align":"center"}
  ]
}
```

## Data layer — Axiom

### Dataset

Single dataset: `rcpapercolor`. Retention: default (30 d) — historical events
become the sparkline raw data later.

### Event schema

```jsonc
{
  "_time":   "<auto>",
  "kind":    "page",       // reserved for future "alert", "control", etc.
  "id":      "claude",     // unique per page kind; latest event per id wins
  "ttl_sec": 3600,         // ignored by firmware in v1; documents producer cadence
  "order":   10,           // optional; lower sorts earlier in rotation
  "layout":  { /* declarative tree */ }
}
```

### Firmware APL query

Sent verbatim from `config.h`. The string is the only Axiom-specific knowledge
in the firmware.

```kusto
['rcpapercolor']
| where ['kind'] == 'page' and _time > ago(1h)
| summarize arg_max(_time, *) by ['id']
| project ['id'], ['order'], ['layout']
| order by ['order'] asc nulls last, ['id'] asc
```

Response shape (Axiom): `{matches: [{data: {id, order, layout}}, ...]}`. The
firmware extracts `matches[*].data` into the rotation list.

### Auth

Bearer token in `config.h`. Token rotation = edit + reflash. Acceptable for a
single personal device.

## Firmware

### Stack

- PlatformIO, `platform = espressif32 @ 6.12.0`, `board = esp32s3box`,
  `board_build.arduino.memory_type = qio_opi`, `partitions = default_16MB.csv`.
- Libs: `M5Unified`, `M5GFX`, `M5PM1`, `ArduinoJson@^7.4`.
- EPD refresh mode: `epd_text` for text-heavy frames, `epd_quality` only when
  an `image` primitive is present in the current page.

### Boot/wake loop

```cpp
on wake:
  M5.begin(cfg)
  reason  = esp_sleep_get_wakeup_cause()
  state   = nvs_load()    // { page_index, last_fetch_ts, cached_pages_blob }

  needs_fetch = (reason == BUTTON_C)
             || (now - state.last_fetch_ts >= FETCH_INTERVAL)
             || (state.cached_pages_blob is empty)

  if needs_fetch:
      connect_wifi()
      pages = axiom_query()                  // APL POST
      if pages.ok:
          state.cached_pages_blob = serialize(pages)
          state.last_fetch_ts     = now
          state.page_index        = 0
          nvs_save(state)

  switch reason:
      case BUTTON_A:   state.page_index = (state.page_index + 1) % len(pages)
      case BUTTON_B:   state.page_index = (state.page_index - 1) % len(pages)
      case RTC_ALARM:  state.page_index = (state.page_index + 1) % len(pages)
      default:         /* keep current */

  stale = (now - state.last_fetch_ts) > FETCH_INTERVAL
  render(pages[state.page_index], stale)
  nvs_save(state)

  enable_button_wake(A, B, C)
  next = min(state.last_fetch_ts + FETCH_INTERVAL, now + ROTATE_INTERVAL)
  esp_deep_sleep_until(next)
```

### Render engine

Two passes:

1. **Layout pass**: walks the tree, assigns each node a `Rect{x,y,w,h}`.
   `vstack/hstack/spacer/divider` follow flex rules; content nodes ask
   primitives for their natural size given current font/icon metrics.
2. **Paint pass**: dispatches `(node, rect) → drawNode(...)` per primitive.
   Each primitive uses `M5GFX` calls bound to the Spectra 6 palette.

### Fonts + icons

- Fonts: ship one sans-serif family with four sizes (h1≈36, h2≈26, body≈18,
  sm≈12). Bold variant only at h1/body. M5GFX-provided fonts where possible to
  avoid baking custom TTFs.
- Icons: bitmap atlas compiled into flash. Registry maps `name → bitmap + size
  hints`. v1 set: `claude`, `codex`, `wifi`, `wifi_off`, `battery`, `warning`.
  Unknown names render as a placeholder box.

### Cached pages — NVS

- Key: `pages_blob`. Value: raw JSON bytes (the APL response's `matches[*].data`
  array). Max ~16 KB; budget per page ~4 KB. Reject oversized payloads with a
  visible error page rather than crashing.
- Key: `page_index` (u8). Key: `last_fetch_ts` (u64).
- NVS writes only on successful fetch (≤96/day) — well under flash wear limits.

### Sleep + wake sources

- **RTC alarm** via `esp_sleep_enable_timer_wakeup(next_us)` — primary timer.
- **Button** via `esp_sleep_enable_ext1_wakeup(BTN_A|BTN_B|BTN_C mask,
  ESP_EXT1_WAKEUP_ANY_LOW)`.
- M5PM1 EPD power should be cut before `esp_deep_sleep_start()` to honour the
  battery budget. Use the M5Unified helper rather than poking PM1 registers
  directly (see PaperColor obsidian note's "不要踩的坑" list).

### Error & stale handling

- Fetch failure (network down, Axiom 5xx, JSON parse error) → keep cached
  pages, render with `stale = true`, increment failure counter in NVS (for
  future surfacing), sleep normally.
- Empty rotation list (after fetch) → render a built-in "no pages" frame with
  the last fetch error string.
- Layout parse error on a single page → render a built-in "bad page" frame
  showing the error inline; other pages keep rotating.

## Producer (Mac)

### Stack

- Python ≥ 3.13, managed by `uv`. Single `pyproject.toml` in `producer/`.
- Runtime: launchd plist per script, schedule every 900 s.
- Modules under `rcpaper.sources.*` implement provider fetches; each script in
  `producer/scripts/` wires one source + `rcpaper.layout` + `rcpaper.axiom`.

### Layout DSL

A typed Python helper in `rcpaper.layout` builds the JSON tree:

```python
from rcpaper.layout import vstack, hstack, text, hbar, sparkline, divider, spacer

page = vstack(pad=14, gap=6, children=[
    text("CLAUDE", size="h1", color="white", bg="red", pad=8, align="center"),
    hstack(gap=8, align="center", children=[
        text(f"{five_h_pct}%", size="h1", color="blue"),
        hbar(pct=five_h_pct, color="blue", height=14),
    ]),
    text(f"5H · resets {five_h_reset}", size="sm"),
    sparkline(values=daily_totals_last_7, color="blue", height=40),
    spacer(),
    text(f"updated {now:%H:%M} · {plan_name}", size="sm", align="center"),
])
```

Producer scripts publish via `rcpaper.axiom.ingest(id="claude", layout=page)`.

### V1 sources

- **claude**: read OAuth tokens from `~/.claude/.credentials.json` (the file
  Claude Code already maintains), refresh on near-expiry via the Anthropic
  OAuth token endpoint, then `GET /api/oauth/usage` (per eInkClaude's
  `main.cpp` reference). Write the refreshed credentials back to the same
  file so Claude Code itself stays current.
- **codex**: read OAuth tokens from `~/.codex/auth.json` (the file the Codex
  CLI already maintains), refresh on `last_refresh > 8d`, then
  `GET https://chatgpt.com/backend-api/wham/usage` (per CodexBar's
  `docs/codex.md`). Write the refreshed credentials back to the same file.

No new credential store. Each script reuses the file its own CLI already owns.

Each script writes exactly one Axiom event per run, with its own `id`:

```python
# scripts/claude_fetch.py
ingest(id="claude", layout=build_claude_page(usage), order=10)

# scripts/codex_fetch.py
ingest(id="codex",  layout=build_codex_page(usage),  order=20)
```

### Sparkline data

The 7-day sparkline is the producer's job: at the top of each run, query the
last 7 days of the producer's own Axiom events, aggregate to daily totals,
embed as `sparkline.values`. The firmware does no aggregation.

### launchd

One plist per script under `producer/launchd/`. `.example` is committed; user
copies, edits paths, runs `launchctl bootstrap gui/$(id -u) <plist>`.

## Visual direction — magazine

V1 reference style for the Claude/Codex pages:

- **Header**: knockout text on a coloured strip (red for Claude, green for
  Codex, etc.). Plan badge as a pill on the right.
- **Body**: 2–3 KPI rows where a big number (h1) sits beside an `hbar`.
- **Per-model row**: small framed `text` cells via `text.bg`/`text.pad`.
- **Trend**: `sparkline` of daily totals for the last 7 days.
- **Footer**: divider + small row with WiFi/battery indicator (`icon` + text),
  last-update time, page indicator (`n/m`), and the stale badge when applicable.

The protocol does not encode the magazine style — producers compose it. A
later page kind can be entirely different (clock face, weather, calendar).

## Config

`src/config.h` — gitignored. `src/config.h.example` — committed.

```cpp
// WiFi
#define WIFI_SSID            "..."
#define WIFI_PASS            "..."

// Axiom
#define AXIOM_API_URL        "https://api.axiom.co/v1/datasets/rcpapercolor/_apl"
#define AXIOM_TOKEN          "xaat-..."
#define AXIOM_APL_QUERY      "['rcpapercolor'] | where kind == 'page' and _time > ago(1h) | summarize arg_max(_time, *) by id | project id, order, layout | order by order asc nulls last, id asc"

// Behavior
#define FETCH_INTERVAL_SEC   900   // 15 min
#define ROTATE_INTERVAL_SEC  120   //  2 min
#define ROOT_CA_AXIOM        R"(-----BEGIN CERTIFICATE-----\n...)"
```

(One root CA constant for Axiom's TLS chain. Verified procedure mirrors
eInkClaude's approach.)

## Repo layout

```
rcpapercolor/
├── README.md
├── platformio.ini
├── .gitignore                           # config.h, .superpowers/, .pio/, etc.
├── src/
│   ├── main.cpp                         # wake loop + dispatch
│   ├── config.h.example
│   ├── net/{wifi,axiom}.{h,cpp}
│   ├── proto/{schema,parser}.{h,cpp}    # tree model + ArduinoJson parse
│   ├── render/{engine,primitives,fonts_icons}.{h,cpp}
│   ├── store/nvs.{h,cpp}
│   └── power/sleep.{h,cpp}
├── assets/
│   ├── fonts/                           # if custom; otherwise empty
│   └── icons/                           # source PNGs + atlas generator
├── producer/
│   ├── pyproject.toml
│   ├── rcpaper/
│   │   ├── __init__.py
│   │   ├── axiom.py                     # ingest helper
│   │   ├── layout.py                    # vstack/hstack/text/hbar/... helpers
│   │   └── sources/{claude,codex}.py
│   ├── scripts/{claude_fetch,codex_fetch}.py
│   └── launchd/com.rocry.rcpaper.{claude,codex}.plist.example
└── docs/
    └── superpowers/specs/2026-05-17-rcpapercolor-design.md
```

## Operational defaults

| Knob               | Default          | Notes                                          |
|--------------------|------------------|------------------------------------------------|
| Fetch interval     | 15 min           | Per device wake; APL POST + JSON parse + NVS  |
| Rotate interval    |  2 min           | Page-index ++ on each timer wake              |
| Buttons            | A=next, B=prev, C=force fetch | Wakes from deep sleep via ext1   |
| Wake budget/day    | ~816 (96 fetch + 720 rotate) | Battery est. ~6 d on 1250 mAh     |
| Cached pages cap   | ~16 KB JSON     | NVS blob; reject oversized                    |
| TLS root           | Axiom chain only | One PEM in config.h                           |
| NVS namespace      | `rcpaper`        | Keys: `pages_blob`, `page_index`, `last_ts`   |

## Accepted trade-offs

- **Spectra 6 latency** (3–8 s per full redraw): button taps feel sluggish.
  Inherent to the screen; not addressed in v1.
- **Battery ~6 days**: tunable by lengthening rotate interval; we ship a
  responsive default and let users tighten/loosen via `config.h`.
- **Axiom auth in firmware**: token rotation = reflash. Acceptable for a
  personal device.
- **Producer requires Mac awake**: if the Mac sleeps for >1 h, pages fall out
  of the rotation set. Stale badge surfaces this; not "fixed" in v1.
- **No on-device OAuth**: keeps firmware provider-agnostic at the cost of
  shifting credential handling to the producer.
- **One TLS root only**: Axiom's chain. Other hosts (in future producers,
  alerts, etc.) won't be reachable from the firmware until we add more roots.

## Open questions (deferred, not blocking v1)

- Should `kind` other than `page` (e.g. `alert`, `control`) be defined now or
  when the first need surfaces? — defer until a real second kind appears.
- Font choice (M5GFX built-in vs custom TTF) — pick during implementation,
  iterate from there.
- Icon atlas format and authoring workflow — defer to implementation plan.
- Whether to use M5GFX's `epd_fastest` mode for the "stale-refresh" frame to
  signal "still alive" — defer.
- Multi-device support (multiple physical PaperColors viewing different page
  subsets) — out of scope, but the `order`/filter model is friendly to it
  later (firmware could add a "view tag" to the APL query).
