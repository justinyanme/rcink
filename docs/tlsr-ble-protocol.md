# TLSR BLE Protocol

Little-endian unless stated. C constants live in `firmware/tlsr/firmware/src/proto/wire.h`; Python constants live in `producer/rcink/transport/ble_protocol.py`. `host_test wire-constants` is the cross-language contract oracle.

## UUIDs

Base service: `6f636172-1f20-44e0-9b6c-7a0000000020`.

| Characteristic | UUID | Direction |
|---|---|---|
| STATUS | `6f636172-1f20-44e0-9b6c-7a0000000120` | device -> producer |
| XFER_CTL | `6f636172-1f20-44e0-9b6c-7a0000000220` | producer -> device |
| XFER_DATA | `6f636172-1f20-44e0-9b6c-7a0000000320` | producer -> device |
| XFER_ACK | `6f636172-1f20-44e0-9b6c-7a0000000420` | device notify |
| META | `6f636172-1f20-44e0-9b6c-7a0000000520` | device -> producer |
| RINGLOG | `6f636172-1f20-44e0-9b6c-7a0000000620` | device -> producer |

## Advertisement V2

Manufacturer ID: `0xFFFF`. Payload length: 9 bytes.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | payload type `0x02` |
| 1 | 4 | `data_ver` |
| 5 | 2 | `battery_mV` |
| 7 | 1 | signed `temp_c` |
| 8 | 1 | flags |

Flags:

| Bit | Mask | Meaning |
|---:|---:|---|
| 0 | `0x01` | sync window open |
| 1 | `0x02` | low battery (`battery_mV < 2400`) |
| 2 | `0x04` | transfer/render error latched |
| 3 | `0x08` | display busy |

## Control Frames

All `XFER_CTL` frames are 20 bytes. Reserved bytes are zero.

BEGIN (`0x01`):

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | command |
| 1 | 4 | `data_ver` |
| 5 | 2 | compressed payload length |
| 7 | 2 | total chunk count; producer fills, firmware ignores |
| 9 | 4 | zlib CRC32 of compressed payload |
| 13 | 1 | DSL format, `0x01` = zlib JSON v1 |
| 14 | 6 | reserved |

ABORT (`0x02`): command at offset 0, remaining bytes zero.

COMMIT (`0x03`): command at offset 0, `data_ver` at offset 1, remaining bytes zero.

## Data Frames

`XFER_DATA` frame:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | sequence number |
| 2 | n | compressed payload bytes |

Producer currently uses 180 payload bytes per chunk.

## ACK

ACK notification is 4 bytes: `last_seq` `u16`, `code` `u8`, `flags` `u8`.

| Code | Name |
|---:|---|
| `0x00` | `OK_PROGRESS` |
| `0x01` | `OK_BEGIN` |
| `0x02` | `OK_COMMIT` |
| `0x10` | `ERR_GAP` |
| `0x11` | `ERR_CRC` |
| `0x12` | `ERR_INFLATE` |
| `0x13` | `ERR_PARSE` |
| `0x14` | `ERR_RENDER` |
| `0x15` | `ERR_TOO_BIG` |
| `0x16` | `ERR_FORMAT` |
| `0x17` | `ERR_OVERRUN` |
| `0x18` | `ERR_FLASH` |
| `0x20` | `ERR_LOW_BATT` |
| `0x21` | `ERR_BUSY` |

## Limits

Flash slot size is 4096 bytes. Header is 32 bytes. Maximum compressed payload is 4064 bytes. Firmware refuses BEGIN when cached or measured battery is below 2400 mV.

## Dedup (`data_ver`)

`data_ver` is xxhash32 over the *uncompressed* canonical JSON (`canonical_json`: UTF-8, `sort_keys`, compact separators — `producer/rcink/transport/ble_hash.py`). It is computed by the producer only; firmware stores the value from BEGIN verbatim and never recomputes it. On COMMIT, if `data_ver` matches the already-committed slot, firmware ACKs `OK_COMMIT` without re-rendering (content-addressed dedup). Integrity is guarded separately by CRC32 over the *compressed* bytes. Consequence: the producer's canonical serialization is part of the wire contract — it is pinned by `test_canonical_json_wire_stability_pin` in `producer/tests/transport/test_ble_hash.py`; changing it silently breaks dedup, not correctness.
