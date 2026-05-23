# tlsr-layout — Phase 2: Firmware Fork & GATT Service Skeleton

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fork `~/w/tlsr-price-tag` into `tlsr-layout/firmware/`, strip everything quote-specific, rename the build target, and stand up a new GATT service `0x1F20` with stub characteristics. Build must succeed cleanly and emit a `.bin` of comparable size to the price-tag baseline (≤ 200 KB).

**Architecture:** Direct fork. tlsr-layout/firmware/ is a self-contained CMake project. The same `~/w/tc32` toolchain and `~/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk` SDK that build tlsr-price-tag build this one too. No protocol logic in this phase — characteristics are stubs that log writes and return canned responses.

**Tech Stack:** Telink TC32 GCC 4.5.1, B85m BLE SDK, CMake 3.28+, Ninja. C99. No new libraries (uzlib/jsmn/xxhash arrive in Phase 4).

**Spec:** `docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md`

**Source repo to fork:** `/Users/rocry/w/tlsr-price-tag` (read-only reference; do not modify).

**Phase boundary:** Phase 2 produces a buildable firmware that advertises as `TLay_<MAC>` with manufacturer ID `0xFFFF` and exposes service `0x1F20` with 5 stub characteristics. No DSL parsing, no rendering of new content. Existing `epd+time.c` clock scene stays as the boot screen so we know the device is alive.

---

## File structure

```
firmware/
  CMakeLists.txt                    # copied + edited (target rename, drop quote sources)
  Makefile                          # copied + edited (drop quote-preview rules)
  cmake/
    TelinkSDK.cmake                 # copied as-is
    util.cmake                      # copied as-is
    time_template.h.in              # copied as-is
  vendor/
    vendor.c                        # copied as-is
    vendor_include.h                # copied as-is
  src/
    main.c                          # copied as-is
    app.c                           # copied + stripped of SCENE_QUOTE
    app.h                           # copied + stripped
    app_att.c                       # copied + UUID 0x1F10 → 0x1F20 + new char UUIDs
    app_att.h                       # copied + new DP_H_* indices
    app_config.h                    # copied + QUOTE_* defines removed
    app_ui.c                        # copied as-is
    app_ui.h                        # copied as-is
    ble.c                           # copied + adv name change + manuf data layout
    ble.h                           # copied as-is
    ble_service.c                   # copied + quote handlers removed + layout stubs added
    ble_service.h                   # copied + cmd enum updates
    battery.c, battery.h            # copied as-is
    compress.c, compress.h          # copied as-is
    etime.c, etime.h                # copied as-is
    led.c, led.h                    # copied as-is
    log.c, log.h                    # copied as-is
    one_bit_display.c               # copied as-is
    OneBitDisplay.cpp, .h           # copied as-is
    obd.inl                         # copied as-is
    epd/
      epd.c, epd.h                  # copied as-is
      epd_213.c, epd_213.h          # copied as-is
      epd_spi.c, epd_spi.h          # copied as-is
      epd_private.h                 # copied as-is
      board_blozi.h                 # copied as-is
      epd+time.c                    # KEPT — boot screen so the board comes alive
      epd+list.c                    # KEPT — layout-agnostic; useful as a sanity render
      (epd+quote.c REMOVED)
    assets/                         # all font headers copied as-is
    include_override/               # copied as-is
    (runtime_quotes.c REMOVED)
    (runtime_quotes.h REMOVED)
  .gitignore                        # copied from tlsr-price-tag
  README.md                         # new — short build doc + phase status
```

Authoritative source survey is in this plan; do not re-derive from the upstream repo without checking against this list.

---

## Task 1: Fork the source tree

**Files:** copy from `/Users/rocry/w/tlsr-price-tag` into `firmware/`, skipping the quote machinery.

- [ ] **Step 1: Create the fork root**

```bash
mkdir -p /Users/rocry/w/_hw/tlsr-layout/firmware
cd /Users/rocry/w/_hw/tlsr-layout/firmware
```

- [ ] **Step 2: Copy the keep-list directories**

```bash
cp -R /Users/rocry/w/tlsr-price-tag/cmake          ./cmake
cp -R /Users/rocry/w/tlsr-price-tag/vendor         ./vendor
cp -R /Users/rocry/w/tlsr-price-tag/src            ./src
cp     /Users/rocry/w/tlsr-price-tag/CMakeLists.txt ./CMakeLists.txt
cp     /Users/rocry/w/tlsr-price-tag/Makefile       ./Makefile
cp     /Users/rocry/w/tlsr-price-tag/.gitignore     ./.gitignore
```

- [ ] **Step 3: Delete quote-specific source files**

```bash
rm -f src/epd/epd+quote.c
rm -f src/runtime_quotes.c
rm -f src/runtime_quotes.h
rm -f cmake/generate_quotes.py
rm -rf cmake/quotes
# defensively drop anything under src/quotes if it ever existed
rm -rf src/quotes
```

- [ ] **Step 4: Verify the tree shape**

Run: `find . -type f | grep -E '(quote|runtime_quotes)' | sort`
Expected: empty output. If anything matches, remove it.

Run: `ls src/epd/`
Expected: `board_blozi.h  epd.c  epd.h  epd+list.c  epd+time.c  epd_213.c  epd_213.h  epd_private.h  epd_spi.c  epd_spi.h`
(No `epd+quote.c`.)

- [ ] **Step 5: Commit (defer if sandbox blocks .git — the dispatching agent commits)**

```bash
git add firmware/
git commit -m "feat(firmware): fork tlsr-price-tag, strip quote machinery"
```

If `git add` fails with "Operation not permitted" on `.git/index.lock`, stop with a STOP-FOR-REVIEW note — the dispatching agent will commit on your behalf. **Do not** continue past this point with uncommitted work that conflicts with later tasks; if you cannot commit, stop here.

---

## Task 2: Rebrand build target

**Files:**
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/Makefile`
- Modify: `firmware/cmake/TelinkSDK.cmake` (only the macros that hard-code the target name, if any)

The target everywhere is `TLSR-PRICE-TAG`. We rename to `TLSR-LAYOUT`. Search-and-replace.

- [ ] **Step 1: Identify every reference**

```bash
grep -rn "TLSR-PRICE-TAG" .
```

Expected hits: `CMakeLists.txt`, `Makefile`, `cmake/TelinkSDK.cmake` (the `add_bin_target` / `add_ota_target` macros). Note every file before editing.

- [ ] **Step 2: Apply rename across the listed files**

For each file from Step 1, replace every literal `TLSR-PRICE-TAG` with `TLSR-LAYOUT`. Use a deterministic command so the change is auditable:

```bash
grep -rln "TLSR-PRICE-TAG" . | xargs sed -i '' 's/TLSR-PRICE-TAG/TLSR-LAYOUT/g'
```

(On macOS BSD sed; `-i ''` is required.)

- [ ] **Step 3: Drop quote sources from `CMakeLists.txt`**

Inspect `firmware/CMakeLists.txt`. Find any `file(GLOB …)` or explicit `add_executable(…)` that includes `epd+quote.c` or `runtime_quotes.c`. Confirm none remain (they shouldn't, because the files are gone, but check that the CMakeLists doesn't hard-code their paths anywhere — including `QUOTE_FONT_*` options).

If the CMakeLists has `option(ENABLE_QUOTE_FONT_… …)` lines, remove them. If `QUOTES_FILE` / `ENABLE_RUNTIME_QUOTES` / `RUNTIME_QUOTES_FLASH_ADDR` / `QUOTE_ROTATE_INTERVAL_SECONDS` are defined, remove those `option(…)` / `add_compile_definitions(…)` lines.

- [ ] **Step 4: Drop quote rules from `Makefile`**

Inspect `firmware/Makefile`. Remove any rule whose recipe references `quote-preview`, `quotes/`, `runtime_quotes`, or `generate_quotes.py`. Keep the `flash`/`build`/`clean` style rules.

- [ ] **Step 5: Build & verify**

```bash
cd firmware
rm -rf build
cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX="$HOME/w/tc32" \
  -DSDK_PREFIX="$HOME/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk"
cmake --build build --target TLSR-LAYOUT.bin
ls -lh build/src/TLSR-LAYOUT.bin
```

Expected: builds cleanly. Output file present.

If linking fails because some source still references a quote symbol, follow the linker error to the offending file and remove the reference — typical culprits are `app.c`, `ble_service.c`, `app_config.h`. See Tasks 3 and 4 for the systematic strip; this build step can fail until those tasks land.

If the build fails because of *missing* sources (the CMakeLists still listed `epd+quote.c`), that means Step 3 missed an entry. Re-grep and fix.

- [ ] **Step 6: Commit**

```bash
git add firmware/CMakeLists.txt firmware/Makefile firmware/cmake/TelinkSDK.cmake
git commit -m "build(firmware): rename target to TLSR-LAYOUT, drop quote build rules"
```

(Sandbox caveat from Task 1 applies.)

---

## Task 3: Strip quote code from C sources

**Files:**
- Modify: `firmware/src/app.c`
- Modify: `firmware/src/app.h`
- Modify: `firmware/src/app_config.h`
- Modify: `firmware/src/ble_service.c`
- Modify: `firmware/src/ble_service.h`

Strip every reference to quotes so the build links. Stub anything that callers still expect with a no-op that compiles cleanly.

- [ ] **Step 1: Catalogue the references**

```bash
cd firmware
grep -RIn -E "(quote|SCENE_QUOTE|RUNTIME_QUOTES|QUOTE_)" src/ | sort
```

Save the output to `/tmp/quote-refs.txt` for traceability:

```bash
grep -RIn -E "(quote|SCENE_QUOTE|RUNTIME_QUOTES|QUOTE_)" src/ > /tmp/quote-refs.txt || true
wc -l /tmp/quote-refs.txt
```

- [ ] **Step 2: `app_config.h` — remove quote-related defines**

Open `firmware/src/app_config.h`. Delete every `#define` whose name matches `QUOTE_*` or `RUNTIME_QUOTES_*` or `QUOTE_ROTATE_*`. Delete corresponding `#ifdef` blocks (if a block becomes empty, delete the block too).

- [ ] **Step 3: `app.h` and `app.c` — strip `SCENE_QUOTE`**

In `app.h`: find the scene enum (`enum scene_id { … }` or similar). Remove the `SCENE_QUOTE = …` entry. If there is a default scene macro pointing to `SCENE_QUOTE`, repoint it to `SCENE_TIME`.

In `app.c`: find any case `SCENE_QUOTE: …` blocks in the scene-dispatch switch and remove them. Find any quote rotation timer code (`quote_rotate_ms`, `quote_next_index`, callbacks calling `epd_quote_draw_*`) and remove. After edits, the scene dispatcher must compile and select `SCENE_TIME` as the boot scene.

- [ ] **Step 4: `ble_service.h` — replace quote commands with layout stubs**

Open `firmware/src/ble_service.h`. The existing file declares the RxTx-write command enum with values like `CMD_T_QUOTE_BEGIN = 0x10`, `CMD_T_QUOTE_DATA = 0x11`, `CMD_T_QUOTE_COMMIT = 0x12`, `CMD_T_QUOTE_CLEAR = 0x13` (numbers approximate — match what's actually there).

Replace those four entries with a single placeholder block:

```c
// Layout-DSL commands — wire protocol defined in
// docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md §6.1.
// In Phase 2 these are stubs that log + return ACK_OK; the real FSM lands
// in Phase 4.
#define CMD_T_LAYOUT_BEGIN    0x01
#define CMD_T_LAYOUT_ABORT    0x02
#define CMD_T_LAYOUT_COMMIT   0x03
```

Note the value `0x01` for BEGIN — this matches `XFER_BEGIN` in `producer/src/tlsr_layout/protocol.py`. If the upstream enum used different numbering for its quote commands, that does not constrain us; we restart from `0x01`.

- [ ] **Step 5: `ble_service.c` — strip quote handlers**

Locate the `RxTxWrite` callback (or whatever the upstream calls the GATT-write handler for the existing `0x1F1F` characteristic). Inside its switch over the first command byte, remove every `case CMD_T_QUOTE_*:` branch. Replace with three new branches that just log the receipt:

```c
case CMD_T_LAYOUT_BEGIN:
    log_info("CMD_T_LAYOUT_BEGIN len=%u", (unsigned)len);
    return 0;  // success; real handler lands in Phase 4
case CMD_T_LAYOUT_ABORT:
    log_info("CMD_T_LAYOUT_ABORT");
    return 0;
case CMD_T_LAYOUT_COMMIT:
    log_info("CMD_T_LAYOUT_COMMIT len=%u", (unsigned)len);
    return 0;
```

If `log_info` isn't the right macro in `log.h`, use whatever the file uses (it's likely `LOG_INFO` or `bls_log` — match the existing style in the file).

Also remove the `quote_payload_open` boolean and any guards using it; the layout commands do not gate state in Phase 2.

- [ ] **Step 6: Build**

```bash
cd firmware
cmake --build build --target TLSR-LAYOUT.bin
```

Expected: links cleanly. If linker complains about undefined references to `quote_*` symbols, find the caller and remove it (it slipped through Step 1's catalogue). If the build fails because a header still includes `runtime_quotes.h`, remove the include.

Run: `ls -lh build/src/TLSR-LAYOUT.bin`
Expected: ≤ 200 KB.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/app.c firmware/src/app.h firmware/src/app_config.h \
        firmware/src/ble_service.c firmware/src/ble_service.h
git commit -m "feat(firmware): strip quote commands and SCENE_QUOTE"
```

---

## Task 4: New GATT service `0x1F20` with stub characteristics

**Files:**
- Modify: `firmware/src/app_att.c`
- Modify: `firmware/src/app_att.h`
- Modify: `firmware/src/ble_service.c`
- Modify: `firmware/src/ble_service.h`

Spec §5.1 defines five characteristics: `STATUS`, `XFER_CTL`, `XFER_DATA`, `XFER_ACK`, `META`. We register all five as 128-bit UUIDs derived from the spec's base (`6F636172-1F20-44E0-9B6C-7A_xxxxxxxx_20`). In Phase 2 each handler is a stub — STATUS returns canned 16 B, META returns canned 32 B, XFER_CTL/XFER_DATA log writes and succeed, XFER_ACK is unused (no notifications emitted yet).

- [ ] **Step 1: Define the UUID constants**

In `firmware/src/app_att.c`, find the existing UUID byte arrays for `0x1F10` and its sub-characteristics. Above them, add:

```c
// Layout service — see spec §5 for the UUID derivation.
// Base: 6F636172-1F20-44E0-9B6C-7A_xxxxxxxx_20 (little-endian on the wire).
// Bytes in TLSR SDK are LSB-first.
static const u8 LAYOUT_SERVICE_UUID[16] = {
    0x20, 0x00, 0x00, 0x00, 0x00, 0x7A, 0x6C, 0x9B,
    0xE0, 0x44, 0x20, 0x1F, 0x72, 0x61, 0x63, 0x6F,
};

#define LAYOUT_CHAR_UUID(slot) {                                       \
    0x20, (slot), 0x00, 0x00, 0x00, 0x7A, 0x6C, 0x9B,                  \
    0xE0, 0x44, 0x20, 0x1F, 0x72, 0x61, 0x63, 0x6F,                    \
}

static const u8 LAYOUT_CHAR_STATUS_UUID[16]   = LAYOUT_CHAR_UUID(0x01);
static const u8 LAYOUT_CHAR_XFER_CTL_UUID[16] = LAYOUT_CHAR_UUID(0x02);
static const u8 LAYOUT_CHAR_XFER_DATA_UUID[16]= LAYOUT_CHAR_UUID(0x03);
static const u8 LAYOUT_CHAR_XFER_ACK_UUID[16] = LAYOUT_CHAR_UUID(0x04);
static const u8 LAYOUT_CHAR_META_UUID[16]     = LAYOUT_CHAR_UUID(0x05);
```

(If the SDK has a different convention for declaring 128-bit UUIDs — e.g., `TELINK_BLE_UUID128` — match it. The byte order in the literals must be SDK-LSB-first; double-check by comparing with the existing 0x1F10 declaration.)

- [ ] **Step 2: Register attributes in `my_Attributes[]`**

In the same file, locate `static const attribute_t my_Attributes[] = { … };` (or whatever the upstream calls the GATT DB). Below the existing 0x1F10 block, add a 0x1F20 primary-service block followed by five characteristic blocks. Use the upstream's pattern for declaration-value-CCCD triples.

Storage backing for the stubs:

```c
static u8 layout_status_buf[16];           // STATUS read+notify
static u8 layout_meta_buf[32];             // META read-only
static u8 layout_xfer_ctl_scratch[20];     // last write captured for inspection
static u8 layout_xfer_data_scratch[182];   // last data chunk captured (MTU-3-2)
static u8 layout_xfer_ack_cccd[2];         // CCCD for XFER_ACK notifications
static u8 layout_status_cccd[2];           // CCCD for STATUS notifications
```

Initialize `layout_meta_buf` once at boot in `my_att_init()` (added in Step 4) with the spec's META layout:

```
hw_id(2)=0x0001 | fw_ver(4)=phase2 build stamp | epd_w(2)=250 | epd_h(2)=128 |
epd_bpp(1)=2 | palette_mask(2)=BWR | flash_capacity_b(4)=131072 | mtu_pref(2)=185
```

Use little-endian encoding throughout.

- [ ] **Step 3: Add new DP_H_* indices in `app_att.h`**

Find the upstream's `DP_H_*` enum (it sequences the attribute handles for the existing service). Append:

```c
DP_H_LAYOUT_SERVICE = … ,
DP_H_LAYOUT_STATUS_DECL,
DP_H_LAYOUT_STATUS_VAL,
DP_H_LAYOUT_STATUS_CCCD,
DP_H_LAYOUT_XFER_CTL_DECL,
DP_H_LAYOUT_XFER_CTL_VAL,
DP_H_LAYOUT_XFER_DATA_DECL,
DP_H_LAYOUT_XFER_DATA_VAL,
DP_H_LAYOUT_XFER_ACK_DECL,
DP_H_LAYOUT_XFER_ACK_VAL,
DP_H_LAYOUT_XFER_ACK_CCCD,
DP_H_LAYOUT_META_DECL,
DP_H_LAYOUT_META_VAL,
```

Number them by extending the existing enum's last value sequentially.

- [ ] **Step 4: Wire write callbacks in `ble_service.c`**

For `XFER_CTL` and `XFER_DATA`, attribute_t entries get a write callback. Both callbacks are stubs in Phase 2:

```c
static int layout_xfer_ctl_write(rf_packet_att_write_t *p) {
    u16 len = p->l2cap - 3;
    if (len > sizeof(layout_xfer_ctl_scratch)) len = sizeof(layout_xfer_ctl_scratch);
    memcpy(layout_xfer_ctl_scratch, &p->value, len);
    if (len > 0) {
        log_info("LAYOUT XFER_CTL cmd=0x%02X len=%u", p->value, (unsigned)len);
    }
    return 0;
}

static int layout_xfer_data_write(rf_packet_att_write_t *p) {
    u16 len = p->l2cap - 3;
    if (len > sizeof(layout_xfer_data_scratch)) len = sizeof(layout_xfer_data_scratch);
    memcpy(layout_xfer_data_scratch, &p->value, len);
    log_info("LAYOUT XFER_DATA chunk len=%u", (unsigned)len);
    return 0;
}
```

`layout_status_buf` initialisation — fill with zeros at boot, then set `data_ver=0xFFFFFFFF` (= "no page yet"), `battery_mV` from the existing battery API, `temp_c` from the existing temp sensor. Done in `my_att_init()`.

- [ ] **Step 5: Build & verify size**

```bash
cd firmware
cmake --build build --target TLSR-LAYOUT.bin
ls -lh build/src/TLSR-LAYOUT.bin
```

Expected: builds; .bin ≤ 200 KB. Compare against the baseline tlsr-price-tag .bin (~152 KB) — within 5 KB is fine.

- [ ] **Step 6: Sanity-check the symbol set**

```bash
~/w/tc32/bin/tc32-elf-nm build/src/TLSR-LAYOUT | grep -E "^[0-9a-f]+ [TtRrDdBb] (layout_|LAYOUT_)" | sort
```

Expected: at least these symbols are defined: `LAYOUT_SERVICE_UUID`, `LAYOUT_CHAR_STATUS_UUID`, `LAYOUT_CHAR_XFER_CTL_UUID`, `LAYOUT_CHAR_XFER_DATA_UUID`, `LAYOUT_CHAR_XFER_ACK_UUID`, `LAYOUT_CHAR_META_UUID`, `layout_status_buf`, `layout_meta_buf`, `layout_xfer_ctl_write`, `layout_xfer_data_write`.

Also assert no `quote_` / `runtime_quote` symbols remain:

```bash
~/w/tc32/bin/tc32-elf-nm build/src/TLSR-LAYOUT | grep -iE "quote" || echo "no quote symbols"
```

Expected: `no quote symbols`.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/app_att.c firmware/src/app_att.h \
        firmware/src/ble_service.c firmware/src/ble_service.h
git commit -m "feat(firmware): add GATT service 0x1F20 with stub characteristics"
```

---

## Task 5: Advertising payload (LegacyAdv) per spec §5.2

**Files:**
- Modify: `firmware/src/ble.c`
- Possibly: `firmware/src/app_config.h` (advertising name prefix)

- [ ] **Step 1: Find the advertising payload arrays**

```bash
grep -n -E "(tbl_advData|air_tag_adv_data|bls_ll_setAdvData)" firmware/src/ble.c
```

Note every byte array and every call to `bls_ll_setAdvData`.

- [ ] **Step 2: Rewrite the primary advertising payload**

Replace the existing `tbl_advData[]` initializer with the spec §5.2 layout. The MAC suffix is filled at runtime from `mac_public_get()` or equivalent, so the array starts with a stable prefix and the name's last 6 hex chars are patched in `my_att_init()` or wherever the upstream patches its existing name.

```c
// Spec §5.2: 31-byte LegacyAdv with our manufacturer payload.
//   Flags                  AD type 0x01 (3 B):  02 01 06
//   CompleteLocalName      AD type 0x09 (14 B): 0D 09 "TLay_XXXXXX"
//   ManufacturerData       AD type 0xFF (12 B): 0B FF FF FF 02
//                                               data_ver(4) battery_mV(2)
//                                               temp_c(1) flags(1)
// Name MAC suffix and live manufacturer fields are patched at runtime.
static u8 tbl_advData[31] = {
    0x02, 0x01, 0x06,
    0x0D, 0x09, 'T', 'L', 'a', 'y', '_', 'X', 'X', 'X', 'X', 'X', 'X',
    0x0B, 0xFF, 0xFF, 0xFF, 0x02,
    0x00, 0x00, 0x00, 0x00,  // data_ver
    0x00, 0x00,              // battery_mV
    0x00,                    // temp_c
    0x00,                    // flags
};
```

If the upstream had a second `air_tag_adv_data` declaration, delete it and remove its `bls_ll_setAdvData(…)` call — we only emit one advertising payload now.

- [ ] **Step 3: Patch live fields**

Add a helper `layout_refresh_adv_payload()` to `ble.c`. It is called whenever:
- battery / temp updates land, or
- a render completes (will be hooked in Phase 5).

```c
void layout_refresh_adv_payload(void) {
    // Patch MAC suffix
    u8 mac[6];
    extern u8 my_mac_public[6];  // upstream convention; verify name in ble.c
    memcpy(mac, my_mac_public, 6);
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 3; i++) {
        tbl_advData[10 + i*2]     = hex[(mac[5 - i] >> 4) & 0x0F];
        tbl_advData[10 + i*2 + 1] = hex[mac[5 - i] & 0x0F];
    }
    // Patch data_ver from the layout status buffer (Phase 2: zeros)
    memcpy(&tbl_advData[21], &layout_status_buf[0], 4);
    // battery + temp from existing readers
    u16 mv = battery_read_mv();
    tbl_advData[25] = mv & 0xFF;
    tbl_advData[26] = (mv >> 8) & 0xFF;
    tbl_advData[27] = (u8)(s8)temp_read_c();
    // flags: bit0=sync_window_active is set elsewhere
}
```

Call `layout_refresh_adv_payload()` once at the end of `my_att_init()` and before each `bls_ll_setAdvData(tbl_advData, sizeof(tbl_advData))`.

If `battery_read_mv` / `temp_read_c` aren't the exact upstream names, substitute the upstream's.

- [ ] **Step 4: Build & runtime sanity**

```bash
cd firmware
cmake --build build --target TLSR-LAYOUT.bin
```

Expected: clean build.

Then dump the advertising payload bytes from the .bin:

```bash
~/w/tc32/bin/tc32-elf-objdump -s -j .data build/src/TLSR-LAYOUT 2>/dev/null | grep -A2 "tbl_advData" | head -10
```

Expected: bytes start with `02 01 06 0D 09 54 4C 61 79 5F` (`TLay_`). (Not strictly required to verify here; if `objdump -s` doesn't show clearly, skip — the symbol's address can be inspected separately.)

- [ ] **Step 5: Commit**

```bash
git add firmware/src/ble.c
git commit -m "feat(firmware): advertising payload per spec §5.2 (TLay_ + manuf data)"
```

---

## Task 6: README + phase wrap-up

**Files:**
- Create: `firmware/README.md`

- [ ] **Step 1: Write the README**

```markdown
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
```

- [ ] **Step 2: Final build + smoke**

```bash
cd firmware
rm -rf build
cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX="$HOME/w/tc32" \
  -DSDK_PREFIX="$HOME/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk"
cmake --build build --target TLSR-LAYOUT.bin
ls -lh build/src/TLSR-LAYOUT.bin
~/w/tc32/bin/tc32-elf-size build/src/TLSR-LAYOUT
```

Expected: builds; .bin ≤ 200 KB; `text + data` from `size` ≤ 132 KB (must fit in 128 KB flash + small margin; if over, that's a regression vs the tlsr-price-tag baseline of ~125 KB text+data).

- [ ] **Step 3: Commit + tag**

```bash
git add firmware/README.md
git commit -m "docs(firmware): mark phase 2 complete"
git tag -a tlsr-layout-phase2 -m "Phase 2: firmware fork + GATT service 0x1F20"
```

(Sandbox caveat: if tag/commit blocked, hand a STOP-FOR-REVIEW note to the dispatching agent.)

- [ ] **Step 4: Emit phase summary**

Print:

```
PHASE 2 COMPLETE
- firmware/ is a self-contained CMake project
- builds via the same tc32 + b85 SDK as tlsr-price-tag
- TLSR-LAYOUT.bin: <SIZE> bytes
- GATT service 0x1F20 registered with 5 stub characteristics
- advertising name: TLay_<MAC last 3 bytes>, manuf ID 0xFFFF, payload v2
- next: Phase 3 — host-shim render port from rcpapercolor primitives
```

---

## Self-Review Checklist

- [x] **Spec coverage** — Phase 2 covers §5 (BLE service + advertising), partial §7 (build system + firmware structure). §6 (transfer protocol logic), §7.3 (render engine), §7.4 (dependencies uzlib/jsmn/xxhash) deferred to later phases.
- [x] **Placeholder scan** — no TBD / TODO unfilled. Two `if the upstream uses a different name, substitute` notes are deliberate (SDK API names sometimes vary by repo branch); they're guarded by Step 1 grep instructions.
- [x] **Type consistency** — `CMD_T_LAYOUT_BEGIN/ABORT/COMMIT` (0x01/0x02/0x03) match `XFER_BEGIN/ABORT/COMMIT` in `producer/src/tlsr_layout/protocol.py`. `layout_status_buf` size 16 matches spec §5.1 STATUS char size 16 B. `layout_meta_buf` size 32 matches META char size ≤32 B. `layout_xfer_data_scratch` size 182 matches MTU 185 minus 3 ATT header (chunk payload only; the seq prefix sits in `&p->value`).
