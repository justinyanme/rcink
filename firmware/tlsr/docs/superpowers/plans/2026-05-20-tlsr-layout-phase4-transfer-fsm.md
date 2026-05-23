# tlsr-layout — Phase 4: Transfer FSM (BEGIN/DATA/COMMIT, Happy Path)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Wire `CMD_T_LAYOUT_BEGIN` / `XFER_DATA` writes / `CMD_T_LAYOUT_COMMIT` into a real state machine on the firmware. End-to-end: BLE receives the encoded page → flash A/B slot promotion → uzlib decompress → jsmn parse → `run_layout` → `paint_layout` → EPD refresh. Happy path only — recovery (gap/CRC/low-batt) lands in Phase 5.

**Architecture:** Add a new module `firmware/src/proto/xfer.{h,c}` that owns the FSM. Hook it from the existing `ble_service.c` write callbacks. Add `firmware/src/store/slot.{h,c}` for the A/B flash slot management defined in spec §7.1. Vendor `uzlib` and `xxhash32` (small C libs).

**Tech Stack:** C99, Telink B85m SDK flash APIs (`flash_erase_sector`, `flash_write_page`, `flash_read_page`). uzlib for inflate. xxhash32 for `data_ver`. Existing jsmn (Phase 3).

**Spec:** `docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md`

**Phase boundary:** After Phase 4 a Mac can BEGIN → stream chunks → COMMIT a page over BLE and the EPD updates. No retries, no gap detection, no low-battery refusal — those land in Phase 5. The FSM does emit `OK_BEGIN` and `OK_COMMIT` ACKs so the producer's happy-path code is exercisable end-to-end.

---

## File structure

```
firmware/
  src/
    proto/
      xfer.h
      xfer.c                 # FSM: BEGIN/DATA/COMMIT/RENDER states
      crc32.h
      crc32.c                # zlib CRC-32 table-free implementation (~80 B code)
      xxhash32.h
      xxhash32.c             # ~0.5 KB
      uzlib/                 # vendored uzlib (single-translation-unit fork)
        uzlib.h
        defl_static.c
        tinfgzip.c
        tinflate.c
        tinfzlib.c
    store/
      slot.h
      slot.c                 # A/B flash slot logic
    render/
      pipeline.h
      pipeline.c             # parse_layout → run_layout → paint_layout → EPD trigger
```

Each module has a focused responsibility:
- `proto/xfer.c` owns the wire-level state machine but no flash, no inflate.
- `store/slot.c` owns flash addressing + header layout.
- `render/pipeline.c` is the glue: pulls a slot's bytes, runs the full render pipeline, returns a status code.

---

## Task 1: CRC-32 + xxhash32 (small helpers)

**Files:**
- Create: `firmware/src/proto/crc32.h`, `crc32.c`
- Create: `firmware/src/proto/xxhash32.h`, `xxhash32.c`

- [ ] **Step 1: Write `crc32.h`**

```c
#ifndef TLSR_LAYOUT_CRC32_H
#define TLSR_LAYOUT_CRC32_H
#include <stddef.h>
#include <stdint.h>

// Zlib CRC-32 (poly 0xEDB88320). Stateless one-shot variant.
uint32_t crc32_zlib(const uint8_t *data, size_t len);

// Incremental: pass 0 as initial state.
uint32_t crc32_zlib_update(uint32_t state, const uint8_t *data, size_t len);
uint32_t crc32_zlib_finalize(uint32_t state);

#endif
```

- [ ] **Step 2: Write `crc32.c` — table-free Sarwate-bytewise variant**

```c
#include "crc32.h"

uint32_t crc32_zlib_update(uint32_t state, const uint8_t *data, size_t len) {
    uint32_t c = state ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(c & 1u);
            c = (c >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return c ^ 0xFFFFFFFFu;
}

uint32_t crc32_zlib_finalize(uint32_t state) { return state; }

uint32_t crc32_zlib(const uint8_t *data, size_t len) {
    return crc32_zlib_update(0, data, len);
}
```

(Table-free is ~5× slower than a 1 KB table version but TLSR flash is tight and the transfer sizes are small. If profiling shows this is a bottleneck on COMMIT, swap in a Slicing-by-4 table later.)

- [ ] **Step 3: Write `xxhash32.h`/`xxhash32.c`**

Use the public-domain xxHash32 reference implementation (~80 LOC). Function signature:

```c
uint32_t xxhash32(const uint8_t *data, size_t len, uint32_t seed);
```

We always call it with `seed = 0` to match the producer's `xxhash.xxh32(blob).intdigest()`.

Cross-check: `xxhash32((const uint8_t*)"abc", 3, 0)` returns `0x32D153FF` (matches reference vectors).

- [ ] **Step 4: Compile-only verify**

Add the new sources to `firmware/CMakeLists.txt` if not already globbed in. Rebuild:

```bash
cd firmware
cmake --build build --target TLSR-LAYOUT.bin
```

Expected: succeeds.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/proto/crc32.* firmware/src/proto/xxhash32.*
git commit -m "feat(firmware): CRC-32 and xxhash32 helpers"
```

---

## Task 2: Vendor uzlib

**Files:**
- Create: `firmware/src/proto/uzlib/*` (multiple files from upstream pfalcon/uzlib v2.9.x)

- [ ] **Step 1: Vendor the headers + sources**

From pfalcon/uzlib (MIT-licensed), copy these files:

- `uzlib.h`
- `tinflate.c`
- `tinfgzip.c`
- `tinfzlib.c`
- `defl_static.c` (we only inflate; deflate path is small enough to leave in for clean builds)

Place under `firmware/src/proto/uzlib/`. Add the LICENSE file too.

- [ ] **Step 2: Wire CMake**

Update `firmware/CMakeLists.txt` to add the uzlib sources. The fork should compile with the existing TC32 toolchain. If `uzlib.h` references `<assert.h>`/`<string.h>` that the TLSR SDK already provides, leave the includes alone — they resolve via the SDK headers.

- [ ] **Step 3: Build**

```bash
cd firmware && cmake --build build --target TLSR-LAYOUT.bin
ls -lh build/src/TLSR-LAYOUT.bin
```

Expected: builds; size grows by ~3 KB.

- [ ] **Step 4: Add a tiny self-test**

Inside `firmware/host_test/`, add `test_uzlib_decompress.py`:

```python
import zlib
import subprocess
from pathlib import Path

from conftest import ROOT

BIN = ROOT / "build" / "host_test"


def test_uzlib_roundtrip(tmp_path: Path):
    payload = b'{"type":"text","value":"hi"}'
    compressed = zlib.compress(payload, level=9)
    in_path = tmp_path / "in.zlib"
    in_path.write_bytes(compressed)
    out = subprocess.run(
        [BIN, "uzlib-test", str(in_path)], capture_output=True, check=True
    )
    assert out.stdout == payload
```

Extend the host_test binary's CLI to add the `uzlib-test <file>` subcommand: read the file, run uzlib inflate, write decompressed bytes to stdout.

- [ ] **Step 5: Run + commit**

```bash
cd firmware/host_test
cmake --build build
uv run pytest -k uzlib -v
```

Expected: passes.

```bash
git add firmware/src/proto/uzlib/ firmware/CMakeLists.txt \
        firmware/host_test/test_uzlib_decompress.py firmware/host_test/main.c
git commit -m "feat(firmware): vendor uzlib + host roundtrip test"
```

---

## Task 3: A/B flash slot management

**Files:**
- Create: `firmware/src/store/slot.h`
- Create: `firmware/src/store/slot.c`

Implements spec §7.1: two slots at `0x74000` and `0x75000`, 32-byte header, atomic A/B promotion.

- [ ] **Step 1: Write `slot.h`**

```c
#ifndef TLSR_LAYOUT_STORE_SLOT_H
#define TLSR_LAYOUT_STORE_SLOT_H

#include <stdint.h>
#include <stddef.h>

#define SLOT_A_ADDR          0x74000
#define SLOT_B_ADDR          0x75000
#define SLOT_SIZE            0x1000      // 4 KB
#define SLOT_HEADER_SIZE     32
#define SLOT_MAX_PAYLOAD     (SLOT_SIZE - SLOT_HEADER_SIZE)
#define SLOT_MAGIC_BE        0x54504C31u // "TPL1" big-endian

typedef struct {
    uint32_t magic;            // SLOT_MAGIC_BE
    uint32_t data_ver;
    uint16_t uncompressed_len;
    uint16_t compressed_len;
    uint32_t crc32;
    uint32_t render_ts;
    uint32_t seq;              // monotonic across COMMITs
    uint16_t flags;
    uint8_t  reserved[6];
} __attribute__((packed)) slot_header_t;
_Static_assert(sizeof(slot_header_t) == 32, "slot_header_t must be 32B");

typedef enum { SLOT_NONE, SLOT_A, SLOT_B } slot_id_t;

// Read slot header at `addr`. Returns 0 on success, -1 on CRC/magic mismatch.
int slot_read_header(uint32_t addr, slot_header_t *out);

// Read compressed payload bytes into `buf` from slot starting at `addr`.
int slot_read_payload(uint32_t addr, uint8_t *buf, uint16_t len);

// Identify the currently active slot (highest seq with valid CRC over payload).
// Returns SLOT_NONE if both invalid.
slot_id_t slot_active(slot_header_t *out_header);

// Returns SLOT_A or SLOT_B — the one *not* active. Caller writes here.
slot_id_t slot_staging(void);

// Erase the slot (one 4 KB sector erase).
void slot_erase(slot_id_t slot);

// Append payload bytes starting at offset (offset 0 = right after header).
// Caller arranges that writes are flash-page-aligned (256 B pages).
int slot_write_payload(slot_id_t slot, uint16_t offset, const uint8_t *data, uint16_t len);

// Write the header (last operation in a COMMIT — flips the active slot).
int slot_write_header(slot_id_t slot, const slot_header_t *hdr);

// Address of a slot id (helper).
uint32_t slot_addr(slot_id_t slot);

#endif
```

- [ ] **Step 2: Write `slot.c`**

Implementation notes:
- All flash ops go through `flash_erase_sector(addr)`, `flash_write_page(addr, len, buf)`, `flash_read_page(addr, len, buf)` from the B85m SDK.
- `slot_active`: read both headers, accept only those whose magic == `SLOT_MAGIC_BE` AND whose CRC over the payload matches `crc32`. Among valid ones, return the one with the higher `seq`. Tie-breaker: SLOT_A.
- `slot_staging`: returns `SLOT_B` if `slot_active() == SLOT_A`, else `SLOT_A`. If both invalid, return `SLOT_A`.
- `slot_write_payload`: enforce that `offset + len ≤ SLOT_MAX_PAYLOAD`. Writes go to `slot_addr(slot) + SLOT_HEADER_SIZE + offset`.
- `slot_write_header`: writes the 32-byte header at the start of the slot. Must be the **last** write per spec §7.1.

- [ ] **Step 3: Add a deterministic host harness**

Inside `firmware/host_test/`, add a flash stub for the host build:

`firmware/host_test/flash_stub.c`:

```c
#include <stdint.h>
#include <string.h>

#define HOST_FLASH_SIZE  0x80000   // 512 KB synthetic flash
static uint8_t HOST_FLASH[HOST_FLASH_SIZE];

static int host_flash_initialised = 0;

static void host_flash_init(void) {
    if (host_flash_initialised) return;
    memset(HOST_FLASH, 0xFF, sizeof(HOST_FLASH));
    host_flash_initialised = 1;
}

void flash_erase_sector(uint32_t addr) {
    host_flash_init();
    memset(&HOST_FLASH[addr & ~0xFFFu], 0xFF, 0x1000);
}

void flash_write_page(uint32_t addr, uint32_t len, const uint8_t *data) {
    host_flash_init();
    for (uint32_t i = 0; i < len; i++) {
        // NAND-like AND semantics: bits can only flip 1→0 between erases.
        HOST_FLASH[addr + i] &= data[i];
    }
}

void flash_read_page(uint32_t addr, uint32_t len, uint8_t *out) {
    host_flash_init();
    memcpy(out, &HOST_FLASH[addr], len);
}
```

Add `slot.c` and `flash_stub.c` to the host_test CMake build.

- [ ] **Step 4: Tests**

`firmware/host_test/test_slot.py`:

```python
import subprocess
from conftest import ROOT
BIN = ROOT / "build" / "host_test"


def test_slot_roundtrip():
    out = subprocess.run([BIN, "slot-test", "roundtrip"], capture_output=True, check=True)
    assert b"OK" in out.stdout


def test_slot_picks_highest_seq():
    out = subprocess.run([BIN, "slot-test", "highest-seq"], capture_output=True, check=True)
    assert b"OK" in out.stdout


def test_slot_falls_back_on_invalid_crc():
    out = subprocess.run([BIN, "slot-test", "invalid-crc"], capture_output=True, check=True)
    assert b"OK" in out.stdout
```

Implement these scenarios as cases inside `host_test/main.c` under a `slot-test <scenario>` subcommand.

- [ ] **Step 5: Run + commit**

```bash
cd firmware/host_test && cmake --build build && uv run pytest -k slot -v
git add firmware/src/store/ firmware/host_test/flash_stub.c firmware/host_test/main.c \
        firmware/host_test/test_slot.py
git commit -m "feat(firmware): A/B flash slot manager + host roundtrip tests"
```

---

## Task 4: Transfer FSM

**Files:**
- Create: `firmware/src/proto/xfer.h`
- Create: `firmware/src/proto/xfer.c`

The FSM owns transfer state across BLE write callbacks.

- [ ] **Step 1: Write `xfer.h`**

```c
#ifndef TLSR_LAYOUT_PROTO_XFER_H
#define TLSR_LAYOUT_PROTO_XFER_H

#include <stdint.h>

typedef enum {
    XFER_IDLE,
    XFER_RECEIVING,
    XFER_COMMITTED,         // ack pending render
} xfer_state_t;

typedef enum {
    XFER_OK_PROGRESS   = 0x00,
    XFER_OK_BEGIN      = 0x01,
    XFER_OK_COMMIT     = 0x02,
    XFER_ERR_GAP       = 0x10,
    XFER_ERR_CRC       = 0x11,
    XFER_ERR_INFLATE   = 0x12,
    XFER_ERR_PARSE     = 0x13,
    XFER_ERR_RENDER    = 0x14,
    XFER_ERR_LOW_BATT  = 0x20,
    XFER_ERR_BUSY      = 0x21,
} xfer_code_t;

typedef void (*ack_emit_fn)(uint16_t last_seq, uint8_t code, uint8_t flags);

void xfer_init(ack_emit_fn ack);
void xfer_handle_ctl(const uint8_t *frame, uint16_t len);    // 20 B max
void xfer_handle_data(const uint8_t *frame, uint16_t len);   // ≤ MTU-3

// Called from the main loop after a successful COMMIT so the render runs
// outside the BLE callback context (EPD is slow).
void xfer_run_pending_render(void);

xfer_state_t xfer_state(void);

#endif
```

- [ ] **Step 2: Write `xfer.c`**

Behavior (happy-path only this phase):

1. `xfer_handle_ctl` checks the first byte:
   - `0x01` BEGIN: parse fields per spec §6.1. Snapshot `data_ver`, `total_len`, `total_chunks`, `crc32`, `dsl_format`. Reject if `total_len > SLOT_MAX_PAYLOAD` or `total_chunks > 256` — emit `XFER_ERR_BUSY` (placeholder; Phase 5 introduces a richer rejection set). Otherwise: call `slot_erase(slot_staging())`, reset `recv_offset = 0`, `expected_seq = 0`, state = `RECEIVING`, emit `OK_BEGIN`.
   - `0x02` ABORT: state = `IDLE`, no ack (Mac may not be listening).
   - `0x03` COMMIT: verify state == RECEIVING and `recv_offset == total_len`; verify CRC32 over the buffered payload; mark state = COMMITTED; schedule render (set a flag the main loop polls). Do NOT emit `OK_COMMIT` here — it's emitted after render completes in `xfer_run_pending_render` so the Mac can disconnect with a clean signal.
2. `xfer_handle_data`:
   - First 2 bytes = `seq` LE. Rest = payload.
   - If `seq == expected_seq` and `recv_offset + payload_len ≤ total_len`: write to staging slot at `recv_offset`, advance both. Every 8 chunks emit `OK_PROGRESS(last_seq = seq, code = 0)`.
   - Mismatch: ignored in Phase 4 (Phase 5 emits `ERR_GAP`).
3. `xfer_run_pending_render` (called from main loop):
   - If state != COMMITTED, return.
   - Read full payload from staging slot.
   - Allocate two 4 KB scratch buffers (one for inflate output, one for arena). On Phase 4 these are stack-allocated; in Phase 6 we'll evaluate moving to BSS.
   - uzlib inflate. On failure: state = IDLE, emit `ERR_INFLATE`, return.
   - jsmn parse via `parse_layout`. On failure: emit `ERR_PARSE`, return.
   - `run_layout` → `paint_layout`. On exception (shouldn't happen): emit `ERR_RENDER`, return.
   - Write new slot header at staging slot (now becomes active by virtue of its higher `seq`).
   - Trigger EPD refresh (call existing `epd_trigger_refresh()` or equivalent).
   - Emit `OK_COMMIT`, state = IDLE.

Important: the render is the slow part (2–3 s). It MUST run outside the BLE callback. The callback only flips the state flag; `xfer_run_pending_render` runs from `app_main_loop`.

- [ ] **Step 3: Wire from `ble_service.c`**

In the `CMD_T_LAYOUT_*` handlers (added as stubs in Phase 2), call `xfer_handle_ctl(frame, len)`. In the `XFER_DATA` write callback (also Phase 2 stub), call `xfer_handle_data(frame, len)`.

The ACK emitter:

```c
static void layout_send_ack(uint16_t last_seq, uint8_t code, uint8_t flags) {
    uint8_t buf[4];
    buf[0] = last_seq & 0xFF;
    buf[1] = (last_seq >> 8) & 0xFF;
    buf[2] = code;
    buf[3] = flags;
    // Send as a GATT notification on DP_H_LAYOUT_XFER_ACK_VAL
    bls_att_pushNotifyData(DP_H_LAYOUT_XFER_ACK_VAL, buf, sizeof(buf));
}
```

Call `xfer_init(layout_send_ack)` once at boot.

Also: from `app.c`'s main loop, call `xfer_run_pending_render()` once per iteration.

- [ ] **Step 4: Build**

```bash
cd firmware
cmake --build build --target TLSR-LAYOUT.bin
ls -lh build/src/TLSR-LAYOUT.bin
```

Expected: builds. Size growth ~5 KB.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/proto/xfer.* firmware/src/ble_service.c firmware/src/app.c
git commit -m "feat(firmware): BEGIN/DATA/COMMIT transfer FSM (happy path)"
```

---

## Task 5: Render pipeline glue

**Files:**
- Create: `firmware/src/render/pipeline.h`
- Create: `firmware/src/render/pipeline.c`

Encapsulates the "buffered payload → EPD" path that `xfer_run_pending_render` calls.

- [ ] **Step 1: Write `pipeline.h`**

```c
#ifndef TLSR_LAYOUT_RENDER_PIPELINE_H
#define TLSR_LAYOUT_RENDER_PIPELINE_H

#include <stdint.h>

typedef enum {
    PIPE_OK = 0,
    PIPE_ERR_INFLATE,
    PIPE_ERR_PARSE,
    PIPE_ERR_RENDER,
} pipeline_status_t;

// Drive the full pipeline: read compressed bytes from `compressed`,
// inflate into a caller-provided 4 KB buffer, parse, layout, paint.
// `inflate_buf` and `arena_buf` are both 4 KB caller-provided.
pipeline_status_t pipeline_run(
    const uint8_t *compressed, uint16_t compressed_len,
    uint16_t uncompressed_len,
    uint8_t *inflate_buf,
    uint8_t *arena_buf
);

#endif
```

- [ ] **Step 2: Write `pipeline.c`**

```c
#include "pipeline.h"
#include "../proto/uzlib/uzlib.h"
#include "../dsl/parser.h"
#include "../dsl/arena.h"
#include "../engine/engine.h"
#include "render_canvas.h"   // canvas API
#include "paint.h"

pipeline_status_t pipeline_run(
    const uint8_t *compressed, uint16_t compressed_len,
    uint16_t uncompressed_len,
    uint8_t *inflate_buf,
    uint8_t *arena_buf
) {
    // uzlib inflate
    struct uzlib_uncomp ctx;
    uzlib_init();
    ctx.source = compressed;
    ctx.source_limit = compressed + compressed_len;
    ctx.dest_start = ctx.dest = inflate_buf;
    if (uzlib_zlib_parse_header(&ctx) < 0) return PIPE_ERR_INFLATE;
    ctx.dest_limit = inflate_buf + uncompressed_len;
    int r = uzlib_uncompress(&ctx);
    if (r != TINF_DONE) return PIPE_ERR_INFLATE;

    // Parse
    arena_t arena;
    arena_init(&arena, arena_buf, 4096);
    parse_result_t pr;
    layout_node_t *root = parse_layout(
        (const char *)inflate_buf, uncompressed_len, &arena, &pr
    );
    if (!root || pr.status != PARSE_OK) return PIPE_ERR_PARSE;

    // Layout
    rect_t viewport = {0, 0, 250, 128};
    extern const measurer_t measurer_device;   // defined elsewhere (firmware-side)
    laid_out_node_t *laid = run_layout(root, viewport, &measurer_device, &arena);
    if (!laid) return PIPE_ERR_RENDER;

    // Paint
    canvas_set_pixel(0, 0, 1);   // ensure backend is alive (no-op smoke)
    paint_layout(laid);
    return PIPE_OK;
}
```

The `measurer_device` global is provided by the existing `firmware/src/render/fonts.c` (it wraps the font stacks for the on-device measure path).

- [ ] **Step 3: Use it from `xfer_run_pending_render`**

In `xfer.c`, replace the inflate/parse/layout/paint sequence with a single call to `pipeline_run(...)`. Translate `PIPE_ERR_*` → `XFER_ERR_*`.

- [ ] **Step 4: Build**

```bash
cd firmware
cmake --build build --target TLSR-LAYOUT.bin
```

Expected: builds.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/render/pipeline.* firmware/src/proto/xfer.c
git commit -m "feat(render): render pipeline glue (inflate→parse→layout→paint)"
```

---

## Task 6: End-to-end host_test exercise

**Files:**
- Modify: `firmware/host_test/main.c`
- Create: `firmware/host_test/test_pipeline_e2e.py`

- [ ] **Step 1: Add `pipeline-e2e <scenario.json>` subcommand to `main.c`**

The subcommand:
1. Reads the JSON file.
2. zlib-compresses it (use a tiny C zlib like miniz, OR shell out to the producer to pre-compress).
3. Calls `pipeline_run` against host canvas.
4. Writes the PNG output to `out/<scenario>.e2e.png`.

Easier option: pre-compress on the Python side and pass the compressed bytes via stdin.

- [ ] **Step 2: Write `test_pipeline_e2e.py`**

```python
import json
import subprocess
import zlib
from pathlib import Path

import pytest

from conftest import ROOT

BIN = ROOT / "build" / "host_test"
SCENARIOS = sorted((ROOT / "scenarios").glob("*.json"))


@pytest.mark.parametrize("scenario", SCENARIOS, ids=[s.stem for s in SCENARIOS])
def test_e2e_inflate_parse_render(scenario: Path, out_dir: Path):
    raw = scenario.read_bytes()
    compressed = zlib.compress(raw, level=9)
    out_png = out_dir / f"{scenario.stem}.e2e.png"
    subprocess.run(
        [BIN, "pipeline-e2e", "--uncompressed-len", str(len(raw)), "--png", str(out_png)],
        input=compressed,
        check=True,
    )
    assert out_png.exists() and out_png.stat().st_size > 0
```

- [ ] **Step 3: Run + commit**

```bash
cd firmware/host_test
cmake --build build && uv run pytest -k e2e -v
git add firmware/host_test/main.c firmware/host_test/test_pipeline_e2e.py
git commit -m "test(host_test): end-to-end pipeline (inflate→parse→render)"
```

---

## Task 7: Update STATUS char + advertising on render success

**Files:**
- Modify: `firmware/src/proto/xfer.c`
- Modify: `firmware/src/ble.c`

- [ ] **Step 1: On successful render in `xfer_run_pending_render`:**

Update `layout_status_buf[0..3]` (the data_ver field in the STATUS characteristic) to the new `data_ver`. Then call `layout_refresh_adv_payload()` (added in Phase 2) so the next advertisement carries the new hash.

- [ ] **Step 2: Build + commit**

```bash
cd firmware && cmake --build build --target TLSR-LAYOUT.bin
git add firmware/src/proto/xfer.c
git commit -m "feat(firmware): publish new data_ver after successful render"
```

---

## Task 8: Phase wrap-up

- [ ] **Step 1: Full host-test sweep**

```bash
cd /Users/rocry/w/_hw/tlsr-layout/firmware/host_test
cmake --build build
uv run pytest -v
```

Expected: all tests pass (Phase 3 layout + screenshot tests, Phase 4 slot, uzlib, e2e).

- [ ] **Step 2: Firmware build**

```bash
cd /Users/rocry/w/_hw/tlsr-layout/firmware
cmake --build build --target TLSR-LAYOUT.bin
ls -lh build/src/TLSR-LAYOUT.bin
~/w/tc32/bin/tc32-elf-size build/src/TLSR-LAYOUT
```

Expected: `text + data ≤ 145 KB`. If over, the next phase's recovery code (a few KB) will push us close to the limit — flag in the summary.

- [ ] **Step 3: Update README + tag**

```bash
git tag -a tlsr-layout-phase4 -m "Phase 4: BEGIN/DATA/COMMIT transfer FSM (happy path)"
```

Edit `firmware/README.md` Phase status block:
- [x] Phase 4 — BEGIN/DATA/COMMIT transfer FSM.

- [ ] **Step 4: Print summary**

```
PHASE 4 COMPLETE
- Wire protocol BEGIN/DATA/COMMIT implemented end-to-end on firmware.
- A/B flash slots with atomic seq-based promotion.
- uzlib + crc32 + xxhash32 vendored.
- pipeline_run() drives inflate → parse → layout → paint.
- STATUS char + adv data_ver update on render success.
- Host e2e tests: pipeline-e2e per scenario all-green.
- Next: Phase 5 — recovery (gap/CRC/low-batt) and the render-failure ring.
```

---

## Self-Review

- [x] **Spec coverage** — §6.1 frames, §6.2 chunks, §6.4 flow, §7.1 flash. §6.3 ACK codes 0x10–0x21 deferred to Phase 5; only `OK_*` codes emitted here.
- [x] **Placeholder scan** — every code block is complete enough to compile; "implementation outline" notes in xfer.c are explicit about each state transition.
- [x] **Type consistency** — `xfer_code_t` aligns with the producer's `protocol.py` ACK_* constants (0x00/0x01/0x02/0x10/...). `slot_header_t` matches spec §7.1 byte layout (32 B total, magic "TPL1").
