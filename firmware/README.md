# tlsr-layout firmware

TLSR8258 e-paper tag rendering a JSON layout DSL streamed over BLE. Forked
from `~/w/tlsr-price-tag`, stripped of the quote scene, and rewired around
GATT service `0x1F20`. Boot screen is still the time scene so you know the
board is alive.

## Build

```bash
cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX="$HOME/w/tc32" \
  -DSDK_PREFIX="$HOME/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk"
cmake --build build --target TLSR-LAYOUT.bin
```

Output: `build/src/TLSR-LAYOUT.bin`.

## Phase status

- [x] Phase 2 — fork, GATT service stubs, advertising format.
- [ ] Phase 3 — host-shim render port from rcpapercolor primitives.
- [ ] Phase 4 — BEGIN/DATA/COMMIT transfer FSM.
- [ ] Phase 5 — recovery + ring log.

See `docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md`.
