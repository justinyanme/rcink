# tlsr-layout

A Mac-resident BLE producer that streams a JSON layout DSL to a TLSR8258 3-color (Black / White / Red) e-ink price-tag panel, which parses, renders, and deep-sleeps. Sister project to
[rcpapercolor](../rcpapercolor) (6-color ESP32-S3 reference), forked at the firmware level from [tlsr-price-tag](../../tlsr-price-tag).

```
┌──────── Mac (BLE central, "server") ──────────┐    BLE 4.2, MTU≤185
│  tlsr-layout serve / push / devices            │ ──────────────────────┐
│  Python (uv, bleak)                            │                       │
│  layout DSL builders | encode (zlib+CRC) | ble │                       ▼
└────────────────────────────────────────────────┘     ┌── TLSR8258 firmware ─┐
                                                       │ GATT service 0x1F20  │
                                                       │ A/B flash @ 0x74000  │
                                                       │ uzlib + jsmn         │
                                                       │ run_layout + paint   │
                                                       │ EPD 250×128 BWR      │
                                                       └──────────────────────┘
```

## Layout

```
producer/    Python ≥3.13, uv-managed.        89 passing tests, 87 % coverage.
firmware/    C99, TLSR TC32 + B85m SDK.       117 KB binary, ≤145 KB flash.
firmware/host_test/  Host-shim layout +       32 passing tests; PNG dump per scenario.
                     render tests via clang.
docs/superpowers/specs/    Authoritative wire-protocol design (v1).
docs/superpowers/plans/    Per-phase implementation plans (1–8).
```

## Quickstart

### Producer (Mac)

```bash
cd producer
uv sync --all-extras
uv run pytest -q                                  # 89 tests, ~0.4 s

uv run tlsr-layout devices                         # scan 10 s for tags
uv run tlsr-layout push example --device AA:BB:CC:DD:EE:FF
uv run tlsr-layout serve --device-filter AA:BB:CC:DD:EE:FF
```

State persists at `~/.config/tlsr-layout/state.json`.

### Firmware

```bash
cd firmware
cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX="$HOME/w/tc32" \
  -DSDK_PREFIX="$HOME/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk"
cmake --build build --target TLSR-LAYOUT.bin
# build/src/TLSR-LAYOUT.bin
```

### Host layout tests

```bash
cd firmware/host_test
cmake -S . -B build && cmake --build build
uv sync --all-extras
uv run pytest -v                                   # 32 tests, ~0.2 s
# PNGs in firmware/host_test/out/ for visual review.
```

## Phases

| Tag | Scope |
|-----|-------|
| `tlsr-layout-phase1` | Producer skeleton: DSL builders, encode pipeline, dry-run CLI |
| `tlsr-layout-phase2` | Firmware fork from tlsr-price-tag; GATT service 0x1F20 stubs; adv format |
| `tlsr-layout-phase3` | Layout engine (run_layout flex walk) + render primitives + host_test shim |
| `tlsr-layout-phase4` | BEGIN/DATA/COMMIT transfer FSM (happy path) end-to-end |
| `tlsr-layout-phase5` | ERR_GAP/CRC/INFLATE/PARSE/RENDER/LOW_BATT recovery + ring log |
| `tlsr-layout-phase6` | Producer BLE central via bleak (devices, push) |
| `tlsr-layout-phase7` | `serve` daemon + page registry (claude/codex/calendar/todo) |
| `tlsr-layout-phase8` | Power validation (manual hardware checklist) |

## Spec

[`docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md`](docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md) — authoritative wire protocol, GATT layout, advertising format, flash layout, error taxonomy.

## License

Personal project — no upstream contributions invited.
