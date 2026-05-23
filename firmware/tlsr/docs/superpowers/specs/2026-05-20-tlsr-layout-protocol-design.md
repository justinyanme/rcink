# tlsr-layout — Protocol & System Design

**Status:** draft v1 — 2026-05-20
**Author:** rocry (with Claude)
**Sibling projects:** [rcpapercolor](../../../../rcpapercolor) (6-color ESP32-S3 reference), [tlsr-price-tag](../../../../../tlsr-price-tag) (TLSR firmware ancestor)

## 1. Goal

Drive a Telink TLSR8258 3-color (Black / White / Red) e-ink price-tag panel from
a Mac. The Mac runs a long-lived BLE central daemon ("server" in the user's
words) that ships a **layout DSL** — the same JSON layout tree rcpapercolor uses
— to the tag whenever the tag's currently rendered content is stale. The tag
sleeps most of the time, wakes briefly to advertise its state, and only spends
radio on a transfer when the Mac has fresh data.

## 2. Non-goals (v1)

- Multi-page rotation on the tag. Tag stores **one** active DSL blob.
- Encryption / bonding. Personal desk-scale link; OEPL itself ships unencrypted.
- OTA firmware updates. Reuse tlsr-price-tag's existing OTA path if needed.
- More than one tag per Mac. Code keeps a per-MAC state map so multi-tag is a
  later extension, not a v1 feature.
- Cloud / Axiom hop. Producer talks directly to tag over BLE.

## 3. High-level architecture

```
┌──────── Mac (BLE central) ─────────┐         BLE 4.2, MTU≤185
│ producer/  (Python ≥3.13, uv)      │ ───────────────────────────────┐
│   layout/   vstack, text, hbar...  │                                │
│   pages/    claude/codex/cal/...   │                                ▼
│   encode/   zlib(json) + chunker   │      ┌── TLSR8258 firmware ───────────┐
│   ble/      bleak central          │      │ ble/    GATT 0x1F20            │
│   cli       tlsr-layout (typer)    │      │ proto/  BEGIN/DATA/COMMIT FSM  │
└────────────────────────────────────┘      │ store/  flash @ 0x70000/0x71000│
                                            │ dsl/    uzlib + jsmn-style     │
                                            │ render/ port of rcpaper prims  │
                                            │ pm/     PEEK + deep sleep      │
                                            └────────────────────────────────┘
```

**Repo split:** new self-contained repo `tlsr-layout/` with `producer/` (Python)
and `firmware/` (C, forked from tlsr-price-tag and trimmed). No submodule to
the price-tag project — clean break.

## 4. Lifecycle

### 4.1 Steady state (battery-friendly)

1. Tag wakes from deep sleep on RTC timer (default 60 s).
2. Tag advertises for 5 s with manufacturer data containing
   `current_data_ver` (xxhash32 truncated to 32 bits of the last successfully
   rendered uncompressed DSL JSON) plus battery_mV, temp_c, flags.
3. Mac `tlsr-layout serve` is permanently scanning. On seeing a tlsr-layout
   advertisement, it computes the page it *would* serve for that MAC, hashes
   the resulting JSON, and compares.
4. If equal → ignore. Tag's 5 s window expires, deep sleep.
5. If different → connect within the window, run the transfer flow (§6), tag
   renders, deep-sleeps.

### 4.2 Cold boot (battery insert)

Tag holds a 5-minute **sync window** where it advertises continuously. Lets
first-time pairing / initial render succeed without timing pressure. After the
sync window, it sleeps until the next wake policy. Default firmware does not
draw a boot UI; the panel remains unchanged until a DSL page is pushed.
`ENABLE_BLE_PEEK=ON` enables brief periodic advertising windows after the
initial sync window.

### 4.3 State diagram (firmware)

```
BOOT ─► SYNC_WINDOW (5 min advertise, no boot draw)
                  │
                  ├─connected─► TRANSFER_FSM (BEGIN→DATA→COMMIT)
                  │                         │
                  │                         ▼
                  │                 RENDER DSL PAGE
                  │                         │
                  ▼                         ▼
         DEEP_SLEEP / optional PEEK_WAKE ◄──┘
```

Render runs **after** disconnect so the radio is off during the 2–3 s EPD
refresh.

## 5. BLE service

**Service UUID:** `0x1F20` (new; tlsr-price-tag uses `0x1F10`, no collision).

### 5.1 Characteristics

All 128-bit UUIDs derived from the base
`6F636172-1F20-44E0-9B6C-7Axxxxxxxx20` (the leading `6F636172` is ASCII
"ocar"; the trailing `…20` matches the short service code). The per-char
suffix is the 4-byte `xxxxxxxx` slot — `00000001`=`STATUS`, `00000002`=`XFER_CTL`,
`00000003`=`XFER_DATA`, `00000004`=`XFER_ACK`, `00000005`=`META`.

| Char       | Props                       | Size          | Purpose |
|------------|-----------------------------|---------------|---------|
| `STATUS`   | Read, Notify                | 16 B          | Live state — `data_ver(4) | flash_state(1) | last_err(1) | battery_mV(2) | temp_c(1) | flags(1) | reserved(6)`. Mac reads after connect, subscribes during transfer. |
| `XFER_CTL` | Read, Write, Write Without Response | ≤20 B | Frames: `BEGIN`, `ABORT`, `COMMIT`. See §6. |
| `XFER_DATA`| Read, Write Without Response | ≤MTU−3 bytes | Bulk chunks: `seq(2) | payload(…)`. |
| `XFER_ACK` | Read, Notify                | 4 B           | `last_seq(2) | code(1) | flags(1)`. Periodic + on transitions. |
| `META`     | Read                        | ≤32 B         | Static device info — `hw_id(2) | fw_ver(4) | epd_w(2) | epd_h(2) | epd_bpp(1) | palette_mask(2) | flash_capacity_b(4) | mtu_pref(2)`. Cached on Mac after first read. |

### 5.2 Advertising (LegacyAdv, 31 B payload budget)

```
Flags                  AD type 0x01 (3 B):   02 01 06
CompleteLocalName      AD type 0x09 (13 B):  0C 09 "TLay_AABBCC"  ← MAC last 3 bytes as 6 hex
ManufacturerData       AD type 0xFF (13 B):  0C FF FF FF
                                             02                  type tag — adv payload v2
                                             data_ver_hash(4)    xxhash32 of last rendered
                                                                 *uncompressed* DSL JSON
                                             battery_mV(2)
                                             temp_c(1)           int8 °C
                                             flags(1)            bit0=sync_window,
                                                                 bit1=low_batt,
                                                                 bit2=render_err,
                                                                 bit3=display_busy
```

Total = 3 + 13 + 13 = 29 B (fits 31 B). Mac filters on Manufacturer ID
`0xFFFF` + payload type byte `0x02`.

### 5.3 MTU & security

- Tag advertises `mtu_pref=185` in `META` (firmware-side cap, matches B85m
  stack default and macOS CoreBluetooth's typical 185 B effective ATT MTU).
- Mac requests MTU 185 after connect; if the stack negotiates lower, both
  sides honour the negotiated value.
- `XFER_DATA` payload size = `negotiated_mtu − 3 (ATT) − 2 (seq prefix)`. At
  MTU 185 that's 180 B. At legacy MTU 23 it's 18 B (slower fallback).
- No bonding, no encryption in v1. Revisit if tags move out of the personal
  desk threat model.

## 6. Transfer protocol

### 6.1 Control frames (Mac → Tag, `XFER_CTL`, ≤20 B)

```
BEGIN  : 01 | data_ver(4) | total_len(2) | total_chunks(2) | crc32(4) |
              dsl_format(1) | reserved(6)
ABORT  : 02 | reserved(19)
COMMIT : 03 | data_ver(4) | reserved(15)
```

- `data_ver` — xxhash32 of the **uncompressed** DSL JSON. Same value advertised
  by the tag after successful render.
- `total_len` — size of the **compressed** payload, bytes.
- `total_chunks` — ceil(total_len / chunk_payload). Cap 256 (44 KB at MTU 185).
- `crc32` — CRC-32/zlib over the compressed payload. Checked at COMMIT before
  decompress.
- `dsl_format` — `0x01` = zlib(JSON v1). Reserved for future.

### 6.2 Data frames (Mac → Tag, `XFER_DATA`, WriteWithoutResponse)

```
seq(2) | payload(…)
```

Monotonic seq from 0. Total compressed payload = concat in seq order.

### 6.3 Acks (Tag → Mac, `XFER_ACK` notification)

```
last_seq(2) | code(1) | flags(1)
```

| code   | meaning |
|--------|---------|
| `0x00` | `OK_PROGRESS` — emitted every 8 chunks (or last); `last_seq` = highest contiguous seq received |
| `0x01` | `OK_BEGIN` — staging sector erased, ready to receive |
| `0x02` | `OK_COMMIT` — CRC + inflate + parse + persist OK |
| `0x10` | `ERR_GAP` — non-contiguous seq; resume from `last_seq+1` |
| `0x11` | `ERR_CRC` — CRC mismatch on compressed payload |
| `0x12` | `ERR_INFLATE` — uzlib failed |
| `0x13` | `ERR_PARSE` — JSON parser failed |
| `0x14` | `ERR_RENDER` — render or EPD driver path failed (unknown icon, reset/setup failure, display-busy timeout, etc.) |
| `0x20` | `ERR_LOW_BATT` — refused at BEGIN; battery < 2.4 V |
| `0x21` | `ERR_BUSY` — already transferring; ABORT and retry |

### 6.4 Flow

1. Mac writes `BEGIN`; ATT write response is not used. Tag erases staging
   sector, notifies `OK_BEGIN`.
2. Mac streams `XFER_DATA` chunks back-to-back (no per-chunk wait).
3. Tag emits `OK_PROGRESS` every 8 chunks. Mac monitors; if 500 ms goes by
   without progress past the latest chunk it sent, it reads `STATUS` and
   resumes from `last_seq + 1`. Same on `ERR_GAP`.
4. Mac writes `COMMIT`. Tag verifies CRC, decompresses into a 4 KB RAM scratch,
   parses JSON, starts the EPD refresh, waits up to 40 s for `EPD_BUSY` to
   clear, writes the active slot header, publishes the new `data_ver`, then
   notifies `OK_COMMIT`.
5. Mac reads `STATUS`, disconnects, and rescans advertising until it observes
   the committed `data_ver`.

**Why WriteWithoutResponse:** macOS CoreBluetooth round-trip ~30 ms; saving the
ATT handshake keeps BEGIN/DATA/COMMIT behavior consistent. Drops are detected
via protocol ACK notifications, not ATT write responses.

## 7. Firmware design

### 7.1 Flash layout (4 KB sectors, A/B active+staging)

| Address   | Use |
|-----------|-----|
| `0x70000` | **Slot A** — `header(32) | compressed_dsl(…)` (up to 4064 B). |
| `0x71000` | **Slot B** — same layout as Slot A. |
| `0x72000` | **Render-failure ring** — 8 records × 64 B. `ts(4) | err_code(1) | data_ver(4) | partial_json_off(2) | message(53)`. Cheap forensics. |
| `0x73000` | Reserved — tlsr-price-tag quote sector (kept clear in case a board is dual-flashed). |
| `0x74000` | Reserved — Telink SMP pairing sector on 512 KB flash. |
| `0x76000` | Reserved — Telink public MAC sector on 512 KB flash. |
| `0x77000` | Reserved — Telink calibration sector on 512 KB flash. |

**Slot header (32 B):**

```
magic("TPL1")(4) | data_ver(4) | uncompressed_len(2) | compressed_len(2) |
crc32(4) | render_ts(4) | seq(4) | flags(2) | reserved(6)
```

- `crc32` covers `compressed_dsl[0..compressed_len)`.
- `seq` is a monotonic 32-bit counter incremented per successful COMMIT.

**A/B promotion (atomic):**

1. On BEGIN, firmware picks the **inactive** slot (the one whose `seq` is
   lower, or whose CRC is invalid) as staging. Erases it.
2. Streams `XFER_DATA` payload into the slot starting at offset 32.
3. On COMMIT, firmware computes CRC, verifies against the BEGIN-declared
   value, decompresses to a 4 KB RAM scratch, parses (early failures roll
   back: leave header unwritten, `ERR_*` to host).
4. Only after parse OK does firmware write the new header. Header is the last
   32 B written to that sector.
5. Boot picks the slot with the highest `seq` and a valid CRC. Power loss
   between data-write and header-write leaves stale magic / zero CRC in the
   new slot → boot keeps the previous slot. Power loss after header-write =
   already promoted. Never corrupt.

### 7.2 PEEK schedule

Reuses tlsr-price-tag's PEEK infrastructure:

- `BLE_PM_PEEK_ENABLE=1` baseline.
- Compile-time tunables:
  - `-DTLSR_LAYOUT_PEEK_INTERVAL_S=60` (default)
  - `-DTLSR_LAYOUT_ADV_WINDOW_S=5`
  - `-DTLSR_LAYOUT_SYNC_WINDOW_S=300`
- Stretch: adaptive — extend interval to 300 s after N consecutive PEEKs see no
  Mac. Off by default in v1.

### 7.3 Render engine port

Source: `rcpapercolor/src/render/*` (M5GFX, C++). Target: tlsr-price-tag's
existing GFX renderer in `src/epd/` (C, 250×128 BWR buffer, CLASSIC /
WENQUANYI / NOTO_SANS font stacks).

Approach:
- Port the layout pass (flex walk computing `Rect{x,y,w,h}`) as pure C. The
  rcpaper code is small enough and language-agnostic enough to translate
  function-by-function.
- Per-primitive paint into the BWR framebuffer using existing GFX primitives
  for text + drawing. Sparkline / hbar / icon are pixel-level routines that
  don't depend on the M5GFX SDK.
- **Color map** (firmware-side, single source of truth in the producer):
  `black→K, white→W, red→R, yellow→K, green→K, blue→R, transparent→keep`.
  Unknown color name → render-failure log + `ERR_RENDER`.

### 7.4 Dependencies to add to firmware

| Lib       | Purpose                                | Approx size |
|-----------|----------------------------------------|-------------|
| `uzlib`   | zlib inflate                           | ~3 KB code  |
| `jsmn`    | non-validating JSON tokeniser          | ~2 KB code  |
| `xxhash32`| data_ver hashing                       | ~0.5 KB     |

Total well within TLSR8258's 128 KB flash budget after the font-stack realism
check (CLASSIC + WENQUANYI fits; NOTO_SANS stays disabled per
tlsr-price-tag's existing config).

## 8. Producer design

### 8.1 Package layout

```
producer/
  pyproject.toml          uv-managed, python ≥3.13
  src/tlsr_layout/
    __init__.py
    cli.py                typer
    layout.py             vstack/hstack/text/hbar/sparkline/icon/image/...
                          (lifted from rcpaper, 6-color palette names —
                           firmware maps to 3 colors)
    encode.py             json.dumps(ensure_ascii=False) → zlib → chunker
    pages/                claude.py, codex.py, calendar.py, todo.py, quote.py
    ble/
      central.py          bleak scan + connect + GATT writes
      protocol.py         BEGIN/DATA/COMMIT framing, ACK parser, retries
    state.py              ~/.config/tlsr-layout/state.json
  tests/
    test_layout.py
    test_encode.py
    test_protocol.py
    test_pages_snapshot.py
```

### 8.2 CLI surface

```
tlsr-layout serve [--interval 60s] [--device-filter MAC]
    Long-running daemon. Scans, matches by mfgdata, transfers on stale.

tlsr-layout push <page_name> [--device MAC]
    One-shot. Builds the named page (or reads JSON from stdin), connects,
    transfers.

tlsr-layout devices
    10-second scan; prints discovered tlsr-layout tags with battery / temp /
    data_ver / last seen.

tlsr-layout dry-run <page_name>
    Builds + zlibs the page; writes JSON, compressed bytes, and a 250×128 BWR
    PNG (via the firmware host shim — see §9) to /tmp. No BLE.
```

### 8.3 Page assignment

`tlsr-layout serve` rotates through registered pages per device in
round-robin order, advancing once a page has been successfully rendered (i.e.
acknowledged via the next advertisement showing the new `data_ver`). State
persisted in `~/.config/tlsr-layout/state.json` keyed by MAC.

## 9. Testing strategy

- **Producer unit tests** (`pytest`):
  - Layout builder snapshot tests (golden JSON in `tests/golden/`).
  - Encode round-trip (zlib inflate matches input; CRC matches; chunker
    boundary cases at 0, 1, MTU−1, MTU, MTU+1, 8×MTU).
  - ACK parser fuzz (every byte combo for code/flags).
- **Firmware native tests** (CMake host build, mirror tlsr-price-tag's
  `quote-preview` shim):
  - `dsl/parser.c` + `render/*.c` compile as a host dylib. Pytest harness
    feeds JSON, dumps 250×128 BWR framebuffer to PNG, compares against
    golden images in `firmware/test/golden/`.
  - Every primitive gets at least one golden + one negative test.
- **BLE integration tests** (`pytest` + bleak, real hardware):
  - Spin up `serve` against a known fake page, assert the tag's next
    advertisement carries the new `data_ver`.
  - Force `ERR_GAP` by skipping a chunk; assert resume works.
  - Force `ERR_LOW_BATT` (mock battery char); assert refusal + retry.
- **Power tests** (manual, shunt resistor + scope):
  - PEEK_LOOP with no Mac: target ≤30 µA mean.
  - One transfer per hour: target 2+ years on CR2032.

## 10. Error handling matrix

| Scenario | Behavior |
|----------|----------|
| Tag asleep when Mac wants to push | Mac waits — peripherals can't be poked. Tag wakes on its PEEK timer. |
| Transfer aborted mid-flight | Staging sector partial; next BEGIN erases it. Active page unchanged. |
| Mac crashes during transfer | Tag watchdog 30 s → ABORT → peek loop. |
| Battery < 2.4 V at BEGIN | `ERR_LOW_BATT`. Mac backs off, retries next cycle. Logged to ring. |
| DSL parses but render fails | Ring-logged. Previous page kept. Adv `flags.render_err=1` so Mac can warn. |
| EPD busy pin never clears | Ring-logged as `ERR_RENDER`. Previous page kept. Adv `flags.render_err=1`. |
| Out-of-order chunks | `ERR_GAP` with `last_seq` → Mac resumes from `last_seq+1`. |
| Duplicate page push | `data_ver` matches advertised value → Mac never connects. Free dedup. |
| Legacy MTU 23 | Firmware falls back to 20 B chunk payloads. Slower but works. |

## 11. Open items (deferred from v1)

- Multi-page rotation on the tag (timer-driven or button-driven).
- Encryption / per-device shared key.
- Adaptive PEEK interval (300 s after N idle cycles).
- Multi-Mac coordination (one tag, two Macs trying to push different pages).
- OTA layout-renderer update path (today: reflash via tlsr-price-tag OTA flow).

## 12. Implementation phases

The implementation plan (separate document) will sequence:

1. **Producer skeleton** — package, layout module ported from rcpaper, encode
   module, CLI scaffolding, unit tests. No BLE.
2. **Firmware skeleton** — fork tlsr-price-tag, strip quote scene, add new
   GATT service with no-op handlers, advertise stub, deep-sleep / PEEK reused.
3. **Render port** — port primitives + add host-shim test harness; ship golden
   images as gating tests.
4. **Transfer protocol — happy path** — BEGIN/DATA/COMMIT end-to-end with no
   retry logic. Validate against a fake DSL.
5. **Transfer protocol — recovery** — gap detection, CRC, low-batt refusal,
   ring log.
6. **Producer BLE central** — bleak scan, connect, transfer, ACK loop.
7. **`serve` daemon + page registry** — port rcpaper page builders.
8. **Power validation** — measure, tune PEEK interval, ship.
