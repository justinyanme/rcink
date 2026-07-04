# rcink tlsr

Firmware target for the TLSR8258 BWR e-ink tag. The shared Python producer
streams canonical DSL pages over BLE; the tag receives BEGIN/DATA/COMMIT frames,
stores the compressed page in an A/B flash slot, renders it, and advertises the
rendered `data_ver`.

- TLSR8258 / TC32, Telink B85m SDK
- 250x128 black/white/red e-ink panel
- BLE GATT service `0x1F20`
- A/B flash slots at the firmware-defined layout
- Host-testable parser, layout, render, slot, ringlog, and transfer FSM

## Layout

- `firmware/` - TC32 firmware and CMake build
- `firmware/host_test/` - clang host shim and pytest suite
- `../../producer/rcink/transport/ble/` - BLE central protocol implementation
- `../../docs/tlsr-ble-protocol.md` - current wire protocol
- `../../docs/superpowers/ARCHIVE.md` - archived pre-merge phase docs

## Build

```bash
cd firmware/tlsr/firmware
cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX="$HOME/w/tc32" \
  -DSDK_PREFIX="$HOME/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk"
cmake --build build --target TLSR-LAYOUT.bin
```

Output: `firmware/tlsr/firmware/build/src/TLSR-LAYOUT.bin`.

## Host Tests

```bash
cmake -S firmware/tlsr/firmware/host_test -B firmware/tlsr/firmware/host_test/build
cmake --build firmware/tlsr/firmware/host_test/build --target host_test
uv run --directory firmware/tlsr/firmware/host_test pytest
```

The host suite validates layout/render scenarios, retention wake behavior,
ringlog behavior, slot persistence, wire constants, and transfer error paths.
PNG outputs land in `firmware/tlsr/firmware/host_test/out/` for visual review.

## Push

From the repo root:

```bash
cd producer
uv run tlsr-push claude --address AA:BB:CC:DD:EE:FF
```

`tlsr-push` currently sends the TLSR-supported compact pages; grid pages remain
unsupported on this 250x128 target and are skipped by the producer golden
matrix.

## Protocol

See [`../../docs/tlsr-ble-protocol.md`](../../docs/tlsr-ble-protocol.md) for
advertising bytes, frame layouts, ACK codes, size limits, and dedup semantics.
