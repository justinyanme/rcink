# tlsr-layout — Phase 5: Recovery, Error Codes, Failure Ring

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Tighten the Phase 4 FSM with full error-code coverage from spec §6.3: gap detection on `XFER_DATA`, CRC verification at COMMIT, low-battery refusal at BEGIN, render-failure logging to a flash ring buffer, and a watchdog that aborts a stuck transfer. Producer-side already has the matching ACK parsing from Phase 1; this phase makes the firmware emit those codes correctly.

**Architecture:** No new top-level modules — Phase 5 thickens the existing `firmware/src/proto/xfer.c` and adds `firmware/src/store/ringlog.{h,c}` for the render-failure ring at flash address `0x76000`.

**Tech Stack:** Same as Phase 4 — C99, B85m SDK flash + timer APIs.

**Spec:** `docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md` — particularly §6.3 (ACK codes), §6.4 step 3 (gap recovery), §7.1 (failure ring layout), §10 (error matrix).

**Phase boundary:** After Phase 5 the firmware emits every ACK code in §6.3 under the corresponding condition. The producer's recovery code can be exercised end-to-end (`ERR_GAP` → resume, `ERR_CRC` → restart, `ERR_LOW_BATT` → backoff). The failure ring is queryable via a new read-only GATT characteristic so the Mac can fetch the last 8 incidents.

---

## File structure

```
firmware/
  src/
    store/
      ringlog.h
      ringlog.c              # 8-record ring at 0x76000
    proto/
      xfer.c                 # (modified — adds error paths + watchdog)
    ble_service.c            # (modified — new RINGLOG read characteristic)
    app_att.c                # (modified — new char registration)
```

---

## Task 1: Failure ring log

**Files:**
- Create: `firmware/src/store/ringlog.h`
- Create: `firmware/src/store/ringlog.c`

- [ ] **Step 1: `ringlog.h`**

```c
#ifndef TLSR_LAYOUT_STORE_RINGLOG_H
#define TLSR_LAYOUT_STORE_RINGLOG_H

#include <stdint.h>

#define RINGLOG_ADDR         0x76000
#define RINGLOG_RECORD_SIZE  64
#define RINGLOG_CAPACITY     8

typedef struct {
    uint32_t ts;            // ms since boot, or unix-seconds if RTC available
    uint8_t  err_code;      // XFER_ERR_* value
    uint8_t  reserved0;
    uint16_t reserved1;
    uint32_t data_ver;
    uint16_t partial_json_off;
    uint8_t  message[52];   // null-padded; UTF-8
} __attribute__((packed)) ringlog_record_t;
_Static_assert(sizeof(ringlog_record_t) == 64, "ringlog_record_t must be 64B");

void ringlog_init(void);     // call once at boot — walks flash + finds tail
void ringlog_append(uint8_t err_code, uint32_t data_ver, uint16_t partial_off,
                    const char *message);
int  ringlog_read(uint8_t idx_from_newest, ringlog_record_t *out);  // 0 = newest
uint8_t ringlog_count(void);

#endif
```

- [ ] **Step 2: `ringlog.c`**

Layout: 8 records × 64 B = 512 B inside one 4 KB sector at `0x76000`. We never erase mid-use; instead each record's `ts` field doubles as a fill marker (`0xFFFFFFFF` = empty; any non-`0xFFFFFFFF` = used). When all 8 are used, we erase the whole sector on the next append (history is discarded — that's the price of using one sector).

`ringlog_init` walks the 8 slots, finds the index of the first empty one (= write position). If none empty, write_idx = 0 and we'll erase before the next write.

`ringlog_append`: if `write_idx == 0` AND slot 0 is non-empty, erase the sector; then write the new record at `write_idx`, advance `write_idx`.

`ringlog_read(0)` returns the most-recently-written record. `ringlog_read(7)` returns the oldest.

- [ ] **Step 3: Host tests**

`firmware/host_test/test_ringlog.py` — exercise via a new `host_test` subcommand `ringlog-test <scenario>`:

```python
def test_ringlog_basic_append(): ...
def test_ringlog_wraps_after_eight(): ...
def test_ringlog_init_finds_existing_tail(): ...
```

Each scenario calls `ringlog_*` from `main.c` against the synthetic flash stub from Phase 4, asserts on read-back contents.

- [ ] **Step 4: Build + commit**

```bash
cd firmware && cmake --build build --target TLSR-LAYOUT.bin
cd host_test && cmake --build build && uv run pytest -k ringlog -v
git add firmware/src/store/ringlog.* firmware/host_test/main.c \
        firmware/host_test/test_ringlog.py
git commit -m "feat(firmware): render-failure ring log at 0x76000"
```

---

## Task 2: Gap detection on XFER_DATA

**Files:**
- Modify: `firmware/src/proto/xfer.c`

- [ ] **Step 1: Replace the Phase-4 "mismatch ignored" path**

In `xfer_handle_data`:

```c
if (seq != expected_seq) {
    // Out-of-order or duplicate. Emit ERR_GAP with last contiguous.
    ack_emit_(expected_seq == 0 ? 0 : (uint16_t)(expected_seq - 1),
              XFER_ERR_GAP, 0);
    return;
}
```

When the Mac receives `ERR_GAP`, the producer's `protocol.py` (Phase 1) already knows to resend from `last_seq + 1`. Important: do not change `expected_seq` — we still want the missing chunk.

- [ ] **Step 2: Test**

`firmware/host_test/test_xfer_gap.py` — call into a new subcommand `xfer-test gap` that:
1. Initialises the FSM with a captured-ack callback.
2. BEGIN.
3. Sends seq 0, then seq 2 (skipping 1).
4. Asserts the second ACK has code `XFER_ERR_GAP` with `last_seq == 0`.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/proto/xfer.c firmware/host_test/main.c \
        firmware/host_test/test_xfer_gap.py
git commit -m "feat(firmware): emit ERR_GAP on out-of-order XFER_DATA"
```

---

## Task 3: CRC mismatch + inflate/parse/render errors

**Files:**
- Modify: `firmware/src/proto/xfer.c`

- [ ] **Step 1: COMMIT-time CRC check**

In the COMMIT branch (`xfer_handle_ctl` case `0x03`):

```c
uint32_t computed = crc32_zlib(staging_buf, total_len);
if (computed != expected_crc) {
    ringlog_append(XFER_ERR_CRC, data_ver, 0,
                   "crc mismatch");
    ack_emit_(0, XFER_ERR_CRC, 0);
    state_ = XFER_IDLE;
    return;
}
```

We don't have `staging_buf` in RAM (Phase 4 wrote chunks directly to flash). Instead, read back from the staging slot:

```c
uint32_t crc = 0;
uint8_t  buf[256];
uint16_t remaining = total_len;
uint16_t off = 0;
while (remaining > 0) {
    uint16_t n = remaining > sizeof(buf) ? sizeof(buf) : remaining;
    slot_read_payload(slot_addr(staging_slot_) + off, buf, n);
    crc = crc32_zlib_update(crc, buf, n);
    off += n;
    remaining -= n;
}
crc = crc32_zlib_finalize(crc);
if (crc != expected_crc) { /* … */ }
```

- [ ] **Step 2: Inflate / parse / render error paths**

`pipeline_run` already returns `PIPE_ERR_*`. In `xfer_run_pending_render`, translate each into the corresponding `XFER_ERR_*`, append to ringlog, emit ACK, set state = IDLE.

- [ ] **Step 3: Test scenarios**

Add to `host_test`:
- `xfer-test crc-bad` — BEGIN, send full payload but with the COMMIT frame declaring the wrong CRC. Expect `ERR_CRC`.
- `xfer-test inflate-bad` — same, but the payload bytes are garbage (not a valid zlib stream). Expect `ERR_INFLATE`.
- `xfer-test parse-bad` — payload zlib-encodes a non-JSON string. Expect `ERR_PARSE`.

Each scenario asserts the corresponding ACK and that a ringlog record landed.

- [ ] **Step 4: Build + commit**

```bash
cd firmware && cmake --build build --target TLSR-LAYOUT.bin
cd host_test && cmake --build build && uv run pytest -k xfer-test -v
git add firmware/src/proto/xfer.c firmware/host_test/main.c \
        firmware/host_test/test_xfer_*.py
git commit -m "feat(firmware): CRC/inflate/parse/render error ACKs + ringlog"
```

---

## Task 4: Low-battery refusal at BEGIN

**Files:**
- Modify: `firmware/src/proto/xfer.c`
- Modify: `firmware/src/proto/xfer.h` (constants)

- [ ] **Step 1: Add the threshold**

In `xfer.h`:

```c
#define XFER_MIN_BATTERY_MV  2400
```

- [ ] **Step 2: Refuse BEGIN under threshold**

```c
case 0x01: { // BEGIN
    if (battery_read_mv() < XFER_MIN_BATTERY_MV) {
        ringlog_append(XFER_ERR_LOW_BATT, /*data_ver*/ 0, 0, "battery low");
        ack_emit_(0, XFER_ERR_LOW_BATT, 0);
        return;
    }
    /* … existing BEGIN body … */
}
```

- [ ] **Step 3: Test**

Add a host shim hook so `battery_read_mv()` returns a configurable value (e.g., `host_set_battery_mv(2300)`). Add scenario `xfer-test low-batt`:

```python
def test_low_battery_refused(): ...
```

Calls the shim with `2300` mV, sends BEGIN, asserts `ERR_LOW_BATT` and `xfer_state() == IDLE`.

- [ ] **Step 4: Build + commit**

```bash
git add firmware/src/proto/xfer.* firmware/host_test/main.c firmware/host_test/test_xfer_low_batt.py
git commit -m "feat(firmware): refuse BEGIN below 2.4V battery"
```

---

## Task 5: Watchdog (30 s no-progress → ABORT)

**Files:**
- Modify: `firmware/src/proto/xfer.c`

Spec §10 row "Mac crashes during transfer".

- [ ] **Step 1: Add a tick**

Expose `xfer_tick(uint32_t now_ms)` that the main loop calls every loop iteration. Inside:

```c
if (state_ == XFER_RECEIVING && (now_ms - last_chunk_ms_) > 30000u) {
    state_ = XFER_IDLE;
    ringlog_append(XFER_ERR_BUSY, 0, 0, "watchdog 30s no chunk");
    // No ACK — assume the Mac is gone.
}
```

Update `last_chunk_ms_` on every `XFER_DATA` write and on `BEGIN`.

- [ ] **Step 2: Test**

`xfer-test watchdog` scenario:
1. BEGIN.
2. Send one chunk.
3. Advance the host clock by 35 s (the host shim provides `host_set_now_ms`).
4. Call `xfer_tick(now)`.
5. Assert `xfer_state() == XFER_IDLE`, ringlog has the watchdog record.

- [ ] **Step 3: Wire into the firmware main loop**

In `app.c`, alongside `xfer_run_pending_render()`, also call `xfer_tick(etime_ms_now())` (or equivalent — match the existing time API in `etime.c`).

- [ ] **Step 4: Build + commit**

```bash
git add firmware/src/proto/xfer.c firmware/src/app.c firmware/host_test/main.c \
        firmware/host_test/test_xfer_watchdog.py
git commit -m "feat(firmware): 30s no-progress watchdog aborts stuck transfer"
```

---

## Task 6: RINGLOG read characteristic

**Files:**
- Modify: `firmware/src/app_att.c`
- Modify: `firmware/src/app_att.h`
- Modify: `firmware/src/ble_service.c`

Add a 6th characteristic under service `0x1F20` so the Mac can drain the failure ring without flashing.

- [ ] **Step 1: UUID + DP_H index**

In `app_att.c`:
```c
static const u8 LAYOUT_CHAR_RINGLOG_UUID[16] = LAYOUT_CHAR_UUID(0x06);
```

In `app_att.h`: append `DP_H_LAYOUT_RINGLOG_DECL`, `DP_H_LAYOUT_RINGLOG_VAL` after the existing handles.

- [ ] **Step 2: Read callback**

```c
static int layout_ringlog_read(rf_packet_att_read_t *p) {
    // Return the newest record (idx 0). 64 B fixed.
    static ringlog_record_t latest;
    ringlog_read(0, &latest);
    // p->dat is the SDK's response buffer; copy in.
    memcpy(p->dat, &latest, sizeof(latest));
    return sizeof(latest);
}
```

(Match the SDK's actual read-callback signature — the API name varies between B85m SDK versions. Inspect the upstream `bls_att_setAttributeTable` examples or the existing 0x1F1F read callback in the price-tag source.)

- [ ] **Step 3: Register in `my_Attributes[]`**

Add a Characteristic Declaration + Value attribute with `ATT_PERMISSIONS_READ` and the read callback above.

- [ ] **Step 4: Build + commit**

```bash
cd firmware && cmake --build build --target TLSR-LAYOUT.bin
git add firmware/src/app_att.* firmware/src/ble_service.c
git commit -m "feat(firmware): RINGLOG read characteristic (newest record)"
```

---

## Task 7: Adv flags update

**Files:**
- Modify: `firmware/src/ble.c`
- Modify: `firmware/src/proto/xfer.c`

Spec §5.2 advertising flags: bit2 `render_err`. Set bit2 whenever the last transfer ended with any `XFER_ERR_*` code; clear it on the next successful render.

- [ ] **Step 1: Set the flag in `xfer_run_pending_render` / on error**

Maintain a single byte in xfer.c:

```c
static uint8_t adv_flags_ = 0;
void xfer_publish_adv_flags(void); // callable from refresh_adv_payload
uint8_t xfer_adv_flags(void) { return adv_flags_; }
```

On successful render: `adv_flags_ &= ~0x04;`. On any error path: `adv_flags_ |= 0x04;`.

- [ ] **Step 2: Have `layout_refresh_adv_payload` read it**

In `ble.c`:

```c
tbl_advData[28] = (tbl_advData[28] & ~0x04) | (xfer_adv_flags() & 0x04);
```

(Sync the other flag bits — sync_window from existing logic, low_batt from a battery probe — at the same site.)

- [ ] **Step 3: Build + commit**

```bash
cd firmware && cmake --build build --target TLSR-LAYOUT.bin
git add firmware/src/ble.c firmware/src/proto/xfer.c firmware/src/proto/xfer.h
git commit -m "feat(firmware): adv render_err flag on persistent failures"
```

---

## Task 8: Phase wrap-up

- [ ] **Step 1: Full pytest sweep**

```bash
cd /Users/rocry/w/_hw/tlsr-layout/firmware/host_test
cmake --build build && uv run pytest -v
```

Expected: all green. Should include every test from Phases 3 + 4 + 5.

- [ ] **Step 2: Firmware size check**

```bash
cd /Users/rocry/w/_hw/tlsr-layout/firmware
~/w/tc32/bin/tc32-elf-size build/src/TLSR-LAYOUT
```

Expected: text + data ≤ 150 KB. If over, the most likely fat target is the ACK string literals — consider deleting per-error message strings (the ringlog already captures intent).

- [ ] **Step 3: Tag**

```bash
git tag -a tlsr-layout-phase5 -m "Phase 5: error recovery + failure ring"
```

- [ ] **Step 4: Summary**

```
PHASE 5 COMPLETE
- All ACK codes from spec §6.3 implemented and tested.
- ringlog at 0x76000 captures last 8 incidents; readable via new GATT char.
- 30s watchdog aborts orphaned transfers.
- Adv flags.render_err reflects last-transfer status.
- Next: Phase 6 — producer BLE central (bleak).
```

---

## Self-Review

- [x] **Spec coverage** — §6.3 (every ACK code), §6.4 step 3 (gap recovery), §7.1 (ringlog), §10 (every row of the error matrix).
- [x] **Placeholder scan** — every test scenario has a specific expected outcome.
- [x] **Type consistency** — `XFER_ERR_*` enum values match `producer/src/tlsr_layout/protocol.py` ACK constants (0x10/0x11/0x12/0x13/0x14/0x20/0x21).
