# Hardware bring-up findings — 2026-05-20

First-flash session on a Blozi 2.13" BWR Telink TLSR8258 tag previously
running tlsr-price-tag. Used the existing `~/w/telink_burn_tool` BDT
adapter and the new `firmware/build/src/TLSR-LAYOUT.bin` produced by
Phases 2–5.

## What works

1. **Toolchain + build chain**: `cmake --build firmware/build --target TLSR-LAYOUT.bin` produces a 117 KB binary; `tl_check_fw.py` patches it cleanly.
2. **BDT flash path**: `python3 ~/w/telink_burn_tool/telink_tool.py -a -e -f TLSR-LAYOUT.bin` writes 119 940 bytes (0x1D484) over SWS without errors. Reset afterward via `-r` alone (NOT `-a -r` — that re-engages SWS and leaves the chip stuck).
3. **BLE radio comes up**: the chip advertises post-reset.
4. **Producer can see the tag in raw scan** (name `TLay_<MAC last 3>`).
5. **GATT connect** establishes from macOS.

## Confirmed bug #1 — fixed

`firmware/src/ble.c` declared the CompleteLocalName AD with length byte
`0x0D` (claims 13 bytes follow) but the actual content is 1 type byte + 11
ASCII chars = 12 bytes. The off-by-one corrupted the advertisement and
the BLE stack stopped emitting after the first packet (saw ~20 s of adv
post-boot, then silence). Fixed to `0x0C` and verified continuous
advertising afterward (83 hits / 30 s vs 0 before).

## Confirmed bug #2 — open

After connect, macOS CoreBluetooth fails service discovery with:
`Failed to discover descriptors for characteristic 16: Unknown error`
and the tag immediately disconnects. The Layout service `0x1F20` GATT
table in `firmware/src/app_att.c` (lines 337–351) is malformed in a
way that the Telink B85m stack rejects mid-discovery.

Diagnostic narrowing done so far:
- Removing the RINGLOG char (lines 350–351) did NOT fix the disconnect,
  so the issue is not in the last entry alone.
- The 5 remaining chars (STATUS, XFER_CTL, XFER_DATA, XFER_ACK, META)
  also fail.
- Standard SDK services (GAP, GATT, OTA, RxTx, Battery, Temp) all parse
  fine — the disconnect happens specifically when iOS/macOS walks into
  the Layout service block.

Next steps to diagnose (not done in this session):

1. Compare attribute-table byte layout for the layout service against a
   known-working 128-bit-UUID service. The price-tag uses only 16-bit
   UUIDs for everything except OTA, so the OTA service block (lines
   320–326 of price-tag's `app_att.c`) is the closest reference for
   `uuidLen=16` characteristic registration.
2. Try replacing one Layout characteristic at a time with the equivalent
   OTA-shaped declaration (16-bit char value declaration containing the
   128-bit UUID) until discovery survives.
3. The most likely culprit is the `CCCD` placement / count, the
   characteristic-declaration value byte order, or the `attrLen` /
   `pAttrValue` combo for the long-read chars (STATUS=16 B, META=32 B,
   RINGLOG=64 B).

## What is verified end-to-end

- **Host-shim e2e (`firmware/host_test/`)**: 32 pytests pass. Producer
  Python encodes a real page (`tlsr-layout dry-run example`), the
  resulting zlib bytes flow through the same C code (uzlib → jsmn →
  `run_layout` → `paint_layout`) the firmware uses on-device, and a
  250×128 BWR PNG comes out the other side. Layout, palette, primitives
  (text, hbar, sparkline, icon, divider, marker) all visibly correct.
- **Producer (`producer/`)**: 89 pytests pass, 87 % coverage. Includes
  the BLE central code mocked against a `FakePeripheral` that
  implements the spec's wire protocol exactly.

So the entire software pipeline is exercised. The blocker is purely the
on-device GATT registration of the new `0x1F20` service — once that
parses, the existing tested code should drive the EPD.

## Reflash flow that works

```bash
# Build
cd ~/w/_hw/tlsr-layout/firmware
cmake --build build --target TLSR-LAYOUT.bin

# Flash (do NOT chain `-a -r` — that re-engages SWS and stops the chip)
cd ~/w/telink_burn_tool
uv run python3 telink_tool.py -a -e -f /Users/rocry/w/_hw/tlsr-layout/firmware/build/src/TLSR-LAYOUT.bin
uv run python3 telink_tool.py -r
```
