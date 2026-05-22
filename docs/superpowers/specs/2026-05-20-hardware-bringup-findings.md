# Hardware bring-up findings — 2026-05-20

Current state after real TLSR8258 tag validation.

## Verified

- Firmware builds: `make all` in `firmware/` produces
  `build/src/TLSR-LAYOUT.bin` and `.ota`, both ~102 KB.
- Firmware host tests: `cmake --build build && uv run pytest -v` in
  `firmware/host_test/` passes 39 tests.
- Producer tests: `make test` in `producer/` passes 96 tests, coverage
  stays above the configured 85 % floor.
- BDT adapter works over SWS:

```bash
cd ~/w/telink_burn_tool
uv run python3 telink_tool.py -a -e -f /Users/rocry/w/_hw/tlsr-layout/firmware/build/src/TLSR-LAYOUT.bin
uv run python3 telink_tool.py -r
```

- BLE advertising works. Fresh flash advertises `data_ver=0xFFFFFFFF`,
  battery, temperature, and sync-window flag.
- Default firmware is layout-only. Legacy clock/list scenes are opt-in build
  flags; cold boot does not draw a clock. A successful DSL COMMIT switches the
  scene to static image so timer ticks cannot overwrite the pushed page.
- GATT discovery, transfer, and notifications work on macOS CoreBluetooth.
- `tlsr-layout hardware-e2e example` passes after waking the tag:

```text
before C025A605-3CDE-D15F-3C1A-51A733A4C92D data_ver=0xFFFFFFFF batt=3249mV flags=0x09
push ok data_ver=0xE379CEE6 status_data_ver=0xE379CEE6
after C025A605-3CDE-D15F-3C1A-51A733A4C92D data_ver=0xE379CEE6 batt=3256mV flags=0x01
hardware e2e ok: data_ver=0xE379CEE6
```

- Reset persistence works after non-erasing `activate` + `reset`; the active
  slot header hydrates `STATUS` and advertising with the pushed `data_ver`.
- Firmware transfer path calls `EPD_Display(...)`, waits for `EPD_BUSY` to
  clear, then writes the active slot header, publishes `data_ver`, and ACKs
  COMMIT. EPD reset/setup/temp failure returns `ERR_RENDER` instead of
  publishing success.
- Display-busy timeout is fail-closed: a stuck `EPD_BUSY` emits `ERR_RENDER`,
  ring-logs the incident, marks advertising `render_err`, and keeps the old
  active page.
- Advertising flag bit3 now mirrors EPD display-busy state; the hardware gate
  waits for the pushed `data_ver` with display idle and no render error.
- Successful publish re-opens the BLE sync advertising window so post-push
  scanners can observe the final idle `data_ver`.

## Fixed during bring-up

- Advertising AD lengths were wrong; fixed CompleteLocalName length and
  ManufacturerData length.
- Layout flash slots overlapped Telink reserved sectors. Moved:
  `SLOT_A_ADDR=0x70000`, `SLOT_B_ADDR=0x71000`, `RINGLOG_ADDR=0x72000`.
- SMP/security requests broke open macOS GATT sync. Disabled SMP for this
  personal desk-scale v1.
- Connection parameters caused discovery disconnects. Set latency to `0` and
  interval range to 10–20 ms.
- Layout characteristic declarations needed SDK-compatible readable value
  backing and write-without-response transfer semantics.
- ACK notifications sent directly from ATT write callbacks were unreliable.
  Transfer callbacks now enqueue ACKs; `layout_xfer_service_tick()` drains them
  from the main loop.
- Producer COMMIT handling now ignores stale progress ACKs before the final
  COMMIT ACK.
- CLI/serve connect through freshly discovered `BLEDevice` objects instead of
  stale macOS CoreBluetooth UUID strings.
- Boot init now loads the active slot header into `STATUS`; publishing also
  pushes updated advertising data into the BLE stack.
- EPD display setup/reset failure is now fail-closed: transfer reports
  `ERR_RENDER`, ring-logs the incident, and keeps the old active page.
- Legacy clock/list scene default removed from firmware; default scene is
  static image/no-op until a DSL layout is pushed.
- Transfer low-battery gate now mirrors `tlsr-price-tag` runtime quote writes:
  use cached `current_battery_mv` when present, avoiding false `ERR_LOW_BATT`
  from a bad post-suspend ADC sample.

## Current limitation

The terminal-side E2E verifies firmware build, flash, BLE advertising, GATT
push, `STATUS`, post-push advertising, and that `EPD_Display(...)` completed
with `EPD_BUSY` clear before publish/ACK. It does not visually inspect physical
e-ink pixels.

For `example`, compare the tag screen with the host-rendered expected PNG
generated from the exact pushed payload:

```bash
cd producer
uv run tlsr-layout dry-run example --out /tmp/tlsr-layout-e2e-example
cd ../firmware/host_test
./build/host_test pipeline-e2e \
  --uncompressed-len 378 \
  --png /tmp/tlsr-layout-e2e-example/example.expected.png \
  < /tmp/tlsr-layout-e2e-example/example.zlib
```

Expected layout: red header with white `CLAUDE`, black progress bar, red
sparkline, and bottom text `updated 14:32`.

## BDT wake note

After the device has slept or after a push, reset-only can miss the advertising
window on this setup. Reliable wake flow:

```bash
cd ~/w/telink_burn_tool
uv run python3 telink_tool.py -a
uv run python3 telink_tool.py -r
```

Do not combine activation and reset in one command for validation; separate
commands are more reliable on this adapter.
