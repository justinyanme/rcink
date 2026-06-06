# rcink

Umbrella repo for rc e-ink dashboard devices. One DSL, one producer, multiple firmware targets.

## Layout

- `dsl/` — DSL spec (JSON schema + cross-device golden render fixtures).
- `producer/` — shared Python producer (pages, sources, transports, CLI).
- `firmware/s3/` — M5Stack PaperColor (ESP32-S3, 400×600, 6-color). Wi-Fi/Axiom.
- `firmware/t-amoled/` — LILYGO T-Display-S3 AMOLED (ESP32-S3, 240×536, RGB). Wi-Fi/Axiom.
- `firmware/c3-x4/` — Xteink X4 (ESP32-C3, 800×480, B&W). Wi-Fi/Axiom. *(v1 work in progress)*
- `firmware/tlsr/` — TLSR8258 BWR tag (250×128, 3-color). BLE.
- `docs/superpowers/{specs,plans}/` — design specs and implementation plans.

## Design

See [`docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md`](docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md) for the architecture.

## Build

Each firmware target keeps its own build. The umbrella does not unify them.

```bash
# s3 (PlatformIO)
cd firmware/s3 && pio run -e m5stack-papercolor

# t-amoled (PlatformIO)
cd firmware/t-amoled && pio run -e lilygo-t-amoled-dev

# tlsr (CMake + TC32)
cd firmware/tlsr/firmware && cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX="$HOME/w/tc32" \
  -DSDK_PREFIX="$HOME/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk"
cmake --build build --target TLSR-LAYOUT.bin

# producer
cd producer && uv run rcink fetch-all
```

## History

This repo was formed on 2026-05-23 by `git subtree add` from:

- `~/w/_hw/rcpapercolor` (archived at `~/w/_hw/_archive/rcpapercolor-2026-05-23`)
- `~/w/_hw/tlsr-layout`  (archived at `~/w/_hw/_archive/tlsr-layout-2026-05-23`)

History of all files imported via subtree is preserved.
