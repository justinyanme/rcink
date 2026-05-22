# tlsr-layout firmware

TLSR8258 e-paper tag rendering a JSON layout DSL streamed over BLE. Forked
from `~/w/tlsr-price-tag`, stripped of legacy quote/clock/list display paths
from the default build, and rewired around GATT service `0x1F20`. A pushed
layout page renders through the firmware pipeline and publishes its `data_ver`
in BLE advertising. Cold boot does not draw a default UI; the panel stays as-is
until a DSL page is pushed.
Display-driver setup/reset failure or display-busy timeout fails COMMIT with
`ERR_RENDER`; successful COMMIT means `EPD_Display(...)` completed and
`EPD_BUSY` cleared.

## Build

```bash
cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX="$HOME/w/tc32" \
  -DSDK_PREFIX="$HOME/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk"
cmake --build build --target TLSR-LAYOUT.bin
```

Output: `build/src/TLSR-LAYOUT.bin` (currently ~102 KB).

## Phase status

- [x] Phase 2 — fork, GATT service stubs, advertising format.
- [x] Phase 3 — host-shim render port from rcpapercolor primitives.
- [x] Phase 4 — BEGIN/DATA/COMMIT transfer FSM.
- [x] Phase 5 — recovery + ring log.

See `docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md`.

## Host shim

```bash
cd host_test
cmake -S . -B build && cmake --build build
uv sync --all-extras
uv run pytest -v
```

PNG outputs land in `host_test/out/` for visual review. They are not
assertion-gated.
