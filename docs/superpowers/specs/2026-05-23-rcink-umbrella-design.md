# rcink — unified DSL + multi-device renderer design

**Date:** 2026-05-23
**Status:** draft, pending review
**Supersedes scope of:** [rcpapercolor](2026-05-17-rcpapercolor-design.md) (will be folded in), `tlsr-layout` (separate repo, will be folded in)

## Goal

Generalise the rcpapercolor "JSON layout → e-ink" stack into a multi-device platform: one producer, one DSL, three+ independently rendered firmware images. v1 covers three devices spanning all interesting axes (color depth, transport, MCU family):

| Codename | Hardware                                  | Color | Transport     | Buttons |
|----------|-------------------------------------------|-------|---------------|---------|
| `s3`     | M5Stack PaperColor (ESP32-S3, 400×600)    | 6-ink | Wi-Fi (Axiom) | none    |
| `c3-x4`  | Xteink X4 (ESP32-C3, 800×480 GDEQ0426T82) | B&W   | Wi-Fi or BLE  | 4+2 ADC |
| `tlsr`   | TLSR8258 BWR tag (250×128)                | 3-ink | BLE           | none    |

If the abstraction holds across these three, the next devices (Xteink X3, future 7" panels, custom boards) are adapters not redesigns.

## Non-goals (v1)

- **Producer-side device handshake.** The producer never queries device capability. DSL is device-agnostic; each device adapts locally. (Hooks for future telemetry left in protocol but not implemented.)
- **Shared renderer code across MCU families.** v1 keeps three independent renderers — `s3` on M5GFX, `c3-x4` on GxEPD2, `tlsr` on its custom C99 stack — to minimise refactor risk and let each stack stay optimal. Cross-device consistency is enforced by golden tests, not shared code.
- **Xteink X3.** v2. X3's I2C peripherals (BQ27220 fuel gauge, DS3231 RTC, QMI8658 IMU, SD card) are non-trivial and out of v1 scope. X3 still informs the abstraction.
- **OTA, captive portal, on-device config UI.** Device config remains compile-time per build target.
- **Bluetooth mesh / multi-tag fan-out.** v1 assumes one host (Mac) ↔ one BLE device at a time.

## Repo: `rcink`

New umbrella repo at `~/w/_hw/rcink/`. `rcpapercolor` and `tlsr-layout` get folded in via `git subtree add` so history is preserved. Both old repos archived (not deleted) at their current locations.

```
rcink/
├── dsl/
│   ├── SPEC.md                   # human-readable DSL contract (this doc + drift)
│   ├── schema.json               # machine-readable JSON Schema, version tag
│   └── golden/                   # cross-device render fixtures
│       ├── s3/<page>.png         # what s3 should produce for each canonical page
│       ├── c3-x4/<page>.png      # what c3-x4 should produce
│       └── tlsr/<page>.png       # what tlsr should produce
│
├── producer/                     # Python ≥3.13, uv-managed
│   ├── rcink/
│   │   ├── layout.py             # DSL builders (vstack/hstack/text/...)
│   │   ├── palette.py            # semantic + concrete color tokens
│   │   ├── pages/                # claude, codex, calendar, todo, github, overview, ...
│   │   ├── sources/              # claude, codex, calendar, reminders, github, quotes, axiom
│   │   ├── transport/
│   │   │   ├── axiom.py          # Wi-Fi: ingest to Axiom (status quo from rcpaper)
│   │   │   └── ble.py            # BLE: bleak central + BEGIN/DATA/COMMIT (status quo from tlsr)
│   │   ├── devices.py            # `Device` records: id, transport, mac/url
│   │   └── cli.py                # `rcink push <device> <page>`, `rcink serve`, `rcink fetch-all`
│   └── tests/
│
├── firmware/
│   ├── s3/                       # ex-rcpapercolor (whole PlatformIO project)
│   ├── c3-x4/                    # new (PlatformIO, ESP-IDF or Arduino+GxEPD2)
│   └── tlsr/                     # ex-tlsr-layout (whole TC32 project)
│
└── docs/
    └── superpowers/{specs,plans}/    # follow current convention
```

Each firmware subtree keeps its own build system; the umbrella is *not* a unified build. `cd firmware/s3 && pio run` works exactly like today.

## DSL spec

The DSL is JSON. One page = one JSON object. The shape is a tree of typed nodes.

### Top-level page

```json
{
  "version": 1,
  "id": "overview",
  "layout": { "type": "vstack", "children": [...] }
}
```

- `version` — integer. Bumps on breaking change. Devices must reject unknown major version. v1 = `1`. Additive primitives don't bump version; devices skip unknown node types and continue.
- `id` — page identifier. Same id replaces same id (latest-wins per device).
- `layout` — root layout node (always a stack).

### Layout primitives

(Unchanged from rcpaper.)

| `type`    | props                                                          |
|-----------|----------------------------------------------------------------|
| `vstack`  | `gap`, `pad`, `align` (start\|center\|end), `children`         |
| `hstack`  | `gap`, `pad`, `align`, `children`                              |
| `spacer`  | `size` (px; omit → flex)                                       |
| `divider` | `color`, `thickness`                                           |

### Content primitives

(Unchanged from rcpaper; carried into all v1 devices.)

| `type`      | props                                                                                |
|-------------|--------------------------------------------------------------------------------------|
| `text`      | `value`, `size`, `color`, `weight`, `align`, `bg`, `pad`, `max_lines`                |
| `hbar`      | `pct` (0–100), `color`, `height`, `label_left`, `label_right`                        |
| `icon`      | `name` (registry key), `size`, `color`                                               |
| `image`     | `name` (flash asset) **or** `url` (Axiom/HTTP), `width`, `height`                    |
| `sparkline` | `values: number[]`, `color`, `height`, `baseline` (auto\|0)                          |
| `marker`    | `pct`, `color` — vertical tick over previous sibling `hbar`                          |
| `grid`      | `cols`, `rows`, `gap`, `values: number[]`, `width`, `height`                         |

### Semantic tokens

This is the key change. **Colors and sizes are abstract on the wire. Each device resolves them locally.**

#### Color tokens

| Token       | Intent                              | s3 (6-ink) | c3-x4 (B&W) | tlsr (BWR) |
|-------------|-------------------------------------|-----------|--------------|------------|
| `black`     | foreground / text default           | black     | black        | black      |
| `white`     | background / negative space         | white     | white        | white      |
| `accent`    | primary emphasis (urgent / focus)   | red       | black        | red        |
| `warning`   | secondary emphasis (caution)        | yellow    | black        | red        |
| `success`   | positive state                      | green     | black        | black      |
| `info`      | informational tint                  | blue      | black        | black      |
| `muted`     | de-emphasised foreground            | black     | black        | black      |

Legacy concrete tokens (`red`, `yellow`, `green`, `blue`) remain accepted for **backwards compatibility** with existing rcpaper pages and migration ergonomics: each firmware maps them through the same table as the semantic alias they best correspond to (e.g. firmware sees `red` and treats it like `accent`). New pages SHOULD use semantic tokens. The migration policy is one-shot: rewrite rcpaper pages once during folding-in.

#### Size tokens

| Token  | Intent                  | s3 px | c3-x4 px | tlsr px |
|--------|-------------------------|-------|----------|---------|
| `H1`   | big number / page title | 60    | 56       | 24      |
| `H2`   | section header          | 32    | 28       | 16      |
| `Body` | default body            | 20    | 18       | 12      |
| `Sm`   | caption / status bar    | 14    | 12       | 10      |

Concrete pixel values live in each firmware's font registry; producer pages reference the token only.

### Wire format

- Plain UTF-8 JSON.
- Whitespace-stripped (`json.dumps(..., separators=(",", ":"))`).
- BLE transport adds zlib compression + chunking on top — that framing is transport, not DSL.

## Capability model: device-side adaptation

Adaptation responsibilities, all on device:

1. **Color resolution.** Map semantic/concrete token → panel ink via the table above. Unknown token → fallback to `black`.
2. **Size resolution.** Map size token → concrete pixel height via font registry.
3. **Layout.** Run flex pass against panel viewport (`PIPE_VIEWPORT_W/H`).
4. **CJK font swap.** If text contains CJK code points, draw with bundled CJK font; else default.
5. **Truncation.** Text that overflows after layout truncates with `…`. No wrapping (status quo from rcpaper).
6. **Image scaling.** `image` with explicit `width`/`height` is laid out at that size and centre-cropped on the panel. URL-fetched images use cached prior fetch on memory-poor devices (tlsr in particular).

**No producer-side variant pages.** `overview.py` runs once; output is one JSON consumed by all three devices.

## Transport layer

Two transports share the same DSL JSON; framing differs.

### Wi-Fi (Axiom pull)

Status quo from rcpaper.

- Producer: `transport/axiom.ingest(id=..., layout=..., order=N)` posts one event per page to `https://api.axiom.co/v1/datasets/<dataset>/ingest`.
- Device: scheduled wake → HTTPS APL query `arg_max(_time, ...) by id` → JSON per page → cache → render.
- Pull cadence: 30 min default (per device build).
- Manual refresh: device button (if present) triggers immediate APL query.

### BLE (host push)

Status quo from tlsr-layout, generalised.

- Producer: `transport/ble` uses bleak; advertises a `Device` registry (`devices.toml` or similar).
- Common GATT service UUID: existing tlsr-layout base UUID `6f636172-1f20-44e0-9b6c-7a_xxxxxxxx_20`. **All BLE devices reuse this** so producer code is single-target.
- Characteristics: `STATUS`, `XFER_CTL`, `XFER_DATA`, `XFER_ACK`, `META`, `RINGLOG` (unchanged from tlsr-layout phase-1-7 spec).
- Frame format: BEGIN(`data_ver|total_len|total_chunks|crc32|dsl_format|reserved`) → DATA(seq+chunk) → COMMIT(`data_ver|reserved`). zlib-compressed JSON payload.
- ACK every 8 chunks + state changes. Resume on `ERR_GAP` via `STATUS` read.
- Manual refresh from device: button event → write `XFER_CTL` REQUEST(`page_id`) → producer streams that page. (New for v1; not in tlsr-layout's existing protocol.)

### Per-device transport choice

Compile-time selection per firmware build:

| Device  | Default       | Build options                              |
|---------|---------------|--------------------------------------------|
| `s3`    | Wi-Fi/Axiom   | Wi-Fi only (no BLE radio code) for v1      |
| `c3-x4` | Wi-Fi/Axiom   | BLE selectable via build flag in v1.1      |
| `tlsr`  | BLE only      | BLE only (no Wi-Fi radio)                  |

v1.1 enables c3-x4 BLE; v1 keeps it Wi-Fi to limit scope.

## Producer

Producer becomes the union of rcpaper's and tlsr-layout's current producer code, deduplicated. Most code in tlsr-layout's `producer/` is a fork of rcpaper's — folding-in is mostly choosing the latest and deleting one copy.

### Page registry

`producer/rcink/pages/__init__.py` exposes a registry:

```python
PAGES = {
    "overview": overview.build,
    "claude":   claude.build,
    "codex":    codex.build,
    "calendar": calendar.build,
    "todo":     todo.build,
    "github":   github.build,
    "quote":    quote.build,
    "image":    image.build,
}
```

Each `build()` takes typed dataclass inputs (sourced via `sources/`) and returns a layout dict. Existing rcpaper builders carry over near-verbatim, with `color="red"` → `color="accent"` token renames done once at fold-in.

### Multi-page semantics

- One producer cycle (`rcink fetch-all`) runs every builder → ingests N events (one per page) → router sends per device.
- **Source of truth** differs by transport:
  - **Wi-Fi devices**: Axiom (latest-per-id, queryable via one APL round-trip).
  - **BLE devices**: producer's local registry (Mac); pages pushed serially.
- **Local cache on device** is for button-driven cycling without transport round-trip, sized per device:
  - `s3` v1: 1 slot (passive, no buttons → no cycle need).
  - `c3-x4` v1: ~12 slots (NVS multi-key; bounded by flash budget).
  - `tlsr` v1: 2 slots (A/B atomic-update pattern from tlsr-layout phase-5).
- Cache eviction: LRU by `data_ver`. Fresh fetch overwrites stale entry.

### CLI

```
rcink fetch-all              # run all sources, ingest all pages (Wi-Fi devices auto-pick up)
rcink push <device> <page>   # build one page, push via the device's transport
rcink serve                  # daemon: BLE connect on demand + retry queue (ex-tlsr-layout phase 7)
rcink devices                # list configured devices + last-seen
```

## Firmware: three adapters, one DSL

Each firmware is an independent application. The DSL spec and golden PNG set are the only shared contracts. No shared C/C++ library across MCU families.

### Common firmware responsibilities

Every firmware implements:

1. **Wake** (timer / button / power-on).
2. **Transport read** — fetch latest DSL for active page (Wi-Fi pull or BLE accept).
3. **Page cache** — store ≤N DSL blobs in flash, keyed by `id`, sized per device.
4. **Render** — JSON → layout → paint → EPD blit.
5. **Power** — return to deep sleep.
6. **Buttons** (where applicable):
   - `Up` / `Down` → cycle local cache (no transport hit).
   - `Confirm` → force refresh active page (transport hit).
   - `Power` → sleep / wake.
   - Others reserved.

Button behaviour is optional per device. `s3` and `tlsr` skip it entirely.

### Adapter responsibilities per device

| Concern         | `s3`               | `c3-x4`              | `tlsr`              |
|-----------------|--------------------|----------------------|---------------------|
| EPD library     | M5GFX              | GxEPD2 (GDEQ0426T82) | custom SPI driver   |
| Color palette   | E6 quantize        | binary threshold     | BWR pack            |
| JSON parser     | ArduinoJson        | ArduinoJson          | jsmn (existing)     |
| CJK font        | M5GFX bundled      | embed Wenquanyi 12px | embed WQY (existing)|
| Wi-Fi stack     | esp_wifi           | esp_wifi             | n/a                 |
| BLE stack       | n/a (v1)           | n/a (v1)             | TLSR B85m SDK       |
| Flash slot mgr  | NVS single key     | NVS multi-key (~12)  | sector slots A/B    |

Each renderer ports the `run_layout()` + `paint_*()` semantics. Cross-implementation parity is **enforced by golden PNG comparison**, not by shared source.

### Power & wake (X4 specifically)

X4 is the most interactive device. Power model:

- **Deep sleep** between refreshes (~60 s idle wake-on-button, per X3 OEM analysis).
- **Wake sources**: timer (configurable, default 30 min), GPIO 3 (power button, active LOW), GPIO 2 (button-2 ladder).
- **Page cycle button (Up/Down)**: handled in active state without transport hit. Render swap from flash cache. ~3 s EPD refresh.
- **Refresh button (Confirm)**: triggers a Wi-Fi (or BLE in v1.1) pull, then re-render.
- **Power button**: 1 s hold = sleep; press during sleep = wake.

`s3` and `tlsr` keep simple timer-only wake. Status-quo behaviour.

## Testing

### Producer

- `pytest` over `producer/tests/`:
  - Layout builder unit tests (existing rcpaper + tlsr tests merged).
  - Per-page golden JSON snapshots (deterministic builder output).
  - Source-mocking tests with frozen fixtures.

### Cross-device golden render

Each firmware ships a `host_test/` shim that compiles the renderer + a PNG output target on the host:

- `firmware/s3/host_test/`: renderer + libgd or stb_image_write → 400×600 PNG.
- `firmware/c3-x4/host_test/`: renderer + same → 800×480 PNG.
- `firmware/tlsr/host_test/`: existing rig → 250×128 PNG.

CI:

1. `producer` builds canonical page set (`overview.json`, `claude.json`, …) deterministically.
2. Each firmware's `host_test` renders each page → PNG.
3. Compare against `dsl/golden/<device>/<page>.png`. Diff > threshold = fail.
4. Updating goldens is a deliberate commit step (`make goldens`) — never auto.

The golden set is the contract. Renderer drift between devices is visible immediately.

### Hardware validation

Each device has a `<device>/HW_VALIDATION.md` checklist (carry forward tlsr-layout phase 8 pattern). Manual, but documented.

## Migration plan (rough phases — actual plan goes in `writing-plans` next step)

1. **Bootstrap `rcink/` shell** with `dsl/` and empty producer/firmware skeleton.
2. **Subtree-merge rcpaper** into `firmware/s3/`; **subtree-merge tlsr-layout** into `firmware/tlsr/`.
3. **Extract producer**: pick rcpaper's `producer/rcpaper/` as base (more mature); cherry-pick tlsr-layout's BLE transport code into `producer/rcink/transport/ble.py`. Resolve duplicates.
4. **Define semantic palette + size resolver** in producer; rename rcpaper page color usage to semantic tokens.
5. **Write `firmware/c3-x4/`** — Arduino+GxEPD2 baseline, port `s3`'s `run_layout` to ESP32-C3 build target. **This is the bulk of new firmware code.**
6. **Wire BLE manual-refresh request** path on `tlsr` (small protocol addition; new control code).
7. **Add button handling on `c3-x4`** — ADC ladder read + page cycle + refresh trigger.
8. **Multi-page slot store on `c3-x4` and `tlsr`** (NVS / flash sectors).
9. **Golden PNG harness** wired across all three firmwares.
10. **Archive old repos**: `rcpapercolor` → `_archive/rcpapercolor-2026-05-23` ; `tlsr-layout` → `_archive/tlsr-layout-2026-05-23`. Don't delete.

## Open questions (deferred)

- BLE on c3-x4 (v1.1).
- X3 with full peripheral set (v2).
- On-device clock display while sleeping (X4 has no RTC chip; tied to BQ27220 on X3 only).
- Per-page push priorities / scheduling.
- Battery telemetry surfacing in `overview` page (per-device, since each has different fuel source).
