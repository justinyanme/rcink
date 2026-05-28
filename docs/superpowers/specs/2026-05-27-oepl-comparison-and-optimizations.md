# OpenEPaperLink → rcink: comparison & optimization roadmap

Date: 2026-05-27. Living doc — append learnings as the OEPL study continues.

Reference checkout: `~/w/_hw/OpenEPaperLink/` (jjwbruijn/atc1441/dmitry.gr). Focus = TLSR e-ink path.

## 1. The core idea the user wants to borrow

OEPL decouples **producer** (content) from **consumer** (display) via a **broker** (AP base station) and **content addressing** (MD5). The tag is a dumb, self-sufficient display endpoint; it pulls on its own schedule, skips work it already has, and survives without the producer present.

rcink-TLSR today is the opposite: a **tightly-coupled push**. The Mac is producer + transport + must be physically scanning during the tag's 5 s BLE sync window. No skip logic, no self-redraw, no broker.

The DSL choice is rcink's big structural advantage and must be preserved — see §3.

## 2. Architecture comparison (verified against code, not docs)

| Aspect | rcink-TLSR (current) | OpenEPaperLink |
|---|---|---|
| Direction | **Push** — Mac central connects in-window | **Pull** — tag wakes ~40 s, broadcasts `AvailDataReq` |
| Wire content | **Layout DSL** (zlib-JSON, ~hundreds B–few KB) rendered on-device | **Pre-rasterized bitmap** (KB–100s KB), tag just blits |
| On-tag compute | Full layout engine + fonts (~1.2k LoC, `render/`,`dsl/`) | Minimal: inflate/G5-decode + dither |
| Content addressing | `data_ver`=xxhash32(canonical JSON); **advertised but unused** | `dataVer`=MD5[:8]; **skips transfer if slot matches** |
| Dedup (skip work) | **None** — Mac always transfers, FW always re-renders+full EPD refresh | tag finds matching EEPROM slot → `XFER_COMPLETE`, 0 bytes |
| Transfer | BEGIN/DATA/COMMIT, seq chunks (180 B), ACK every 8, gap-resume, CRC full-retry | 4096 B blocks × 42×99 B parts, 6-byte missing-part bitmask re-request |
| Storage | dual 4 KB slots ping-pong, **one** active page | N (≈10) image slots, content-addressed by MD5 |
| Boot | **no redraw** — relies on e-paper physical retention only | redraws stored slot from flash |
| Broker | none (Mac in-window, or Axiom for the s3/WiFi path only) | ESP32 AP: renders/queues per-tag, streams blocks, web UI |
| Rendezvous | blind 5 s sync window every 60 s + 5 min initial | tag-initiated, AP returns `nextCheckIn` (adaptive) |
| Power | deep-sleep-retn, fixed 60 s wake; ~<10 µA sleep | ~9 µA, adaptive interval (8-sample avg), AP-controlled |

Key file refs (rcink): push `producer/rcink/transport/ble/central.py:44`; FW commit/render `firmware/tlsr/firmware/src/proto/xfer.c:207,322`; boot `firmware/tlsr/firmware/src/app.c:44,201`; slot hdr `firmware/tlsr/firmware/src/store/slot.h:13`.

## 3. What NOT to copy (and why)

- **Block/part bitmask re-request** (OEPL's 4096 B/42-part machinery): built for 100s-KB *bitmaps* over lossy 802.15.4. rcink ships a *DSL* that is 1–2 orders of magnitude smaller; the existing seq+gap-resume is sufficient. Porting it = complexity with no payoff.
- **G5 / CCITT-G4 compression**: only beats zlib on large 1bpp *bitmaps*. rcink's payload is JSON, not a bitmap. Revisit *only* for the `image` primitive's raw pixel payload if image-heavy pages appear (Tier 4, deferred).
- **Per-tag image rendering on the AP** (TFT_eSPI sprites, TrueType on ESP32): rcink renders on the tag from DSL — strictly better for bandwidth and broker simplicity. The broker just forwards bytes.

The lesson from OEPL is the **control plane** (addressing, dedup, broker, adaptive pull), not the **data plane** (bitmap codecs).

## 4. Verified gaps → optimization tiers

### Tier 0 — Content-addressed dedup (headline OEPL lesson; highest value/risk ratio)
`data_ver` already exists end-to-end (computed, advertised, stored in slot header) but **nothing consumes it**.
- **0a Mac-side skip** (producer-only, zero-HW, unit-testable): before connect, read tag's advertised `data_ver`; if == `encode_page(page).data_ver`, skip the whole connect+transfer. Mirrors OEPL "already have it → cancel". Biggest single energy/e-paper-wear win.
- **0b FW-side skip** (defense-in-depth): on COMMIT, if committed `data_ver` == active slot `data_ver`, ack OK and **skip the EPD full refresh**. Avoids ghosting + the ~100–200 mA × 2–3 s refresh on redundant pushes. Host-test validatable; HW sign-off pending.

### Tier 1 — Self-sufficient display endpoint
- **1a Boot redraw**: on cold boot (not deep-retn wake), render active slot to panel so the tag restores itself after power loss / data reflash without the producer. OEPL boot-from-flash analog.
- **1b Stale indicator**: if not updated for N h, render a small "no-signal"/"stale" glyph on-device. OEPL `CUSTOM_IMAGE_LOST_CONNECTION` analog. Low priority.

### Tier 2 — Decouple producer from transport (the user's core vision; larger)
- **2a Base-station broker**: always-on host (ubuntu home server w/ BLE, or the s3, or a Pi) pulls pages from Axiom/producer and pushes to the tag on its schedule — Mac no longer needs to be the in-window central. This is OEPL's AP. Reuses the deferred `serve` daemon (memory item).
- **2b Connectionless BLE broadcast**: tag scans for a broadcaster's BLE-5 extended/periodic adv carrying the DSL; ingests with no connection ("obtained via Bluetooth broadcasting"). TLSR8258 ext-adv support is limited → likely v2.
- **2c Pull inversion**: tag scans for broker's "data-available" beacon and connects only then, replacing blind sync windows. More robust rendezvous.

### Tier 3 — Adaptive power
- **3a `nextCheckIn`**: broker/producer returns next-wake hint in COMMIT ack; tag lengthens sleep when content is static (e.g. overnight). OEPL `nextCheckIn` analog.

### Tier 4 — Compression (deferred)
- **4a** G5/CCITT for the `image` primitive raw payload only, if/when image pages grow.

## 5. Recommended sequencing
Tier 0 first (clear win, mostly software, proves the dedup lesson) → Tier 1a (cheap robustness) → Tier 2 (the decoupling; needs a direction decision: broker vs broadcast) → Tier 3 → Tier 4.

## 6. Status (2026-05-28)

**Shipped + HW-validated** (this session):
- **0a Mac-side skip** — `push_page(..., current_data_ver=...)` short-circuits before any transfer on a hash match (`central.py`). Tests in `test_ble_central_happy.py`. HW: T3 = 0.00 s, **0 GATT writes**.
- **0b FW-side skip** — COMMIT with `data_ver == active slot` acks OK + skips the full EPD refresh (`xfer.c` COMMIT handler). host_test `xfer-test commit-skip-duplicate`. HW: re-push of identical page = **0.23 s vs 1.32 s** render (the ~1.1 s EPD refresh was skipped); a genuinely different page still renders slow (1.22 s), so the skip is content-driven.
- **1a Boot redraw** — cold boot re-renders the stored active slot (`xfer_request_boot_render`/`xfer_run_boot_render` in `xfer.c`, wired in `app.c`; shared `render_slot_payload`). host_test `xfer-test boot-redraw`/`boot-redraw-empty`. HW: after cold boot the slot's `data_ver` persists, `render_err=False`, and the `display_busy` adv flag is caught mid-refresh — the panel repaints itself with no producer present.

Producer: 134 tests green. host_test: 13 xfer + 3 slot scenarios green. Target fw builds (110 KB), flashes, boots.

**Tier 2 direction chosen: base-station broker** (always-on host bridges Axiom→BLE push to the tag on its schedule; Mac no longer needs to be the in-window central).

**Blocking finding for the broker** — `connect_transport` (transport.py) builds the `BleakClient` *after* `discovery.scan()` has closed its scanner; on macOS the `BLEDevice` from a closed scanner goes stale → `BleakDeviceNotFoundError`. The BLE push has unit tests but was never HW-validated end-to-end, so this latent bug was never hit. Working pattern (proven this session): `BleakScanner.find_device_by_filter(...)` keeps the scanner alive until the device is returned connectable, then connect immediately. Now shipped as `discover_tag()` in `discovery.py`; it feeds a fresh device to `connect_transport`. (macOS scan-cache is also flaky for this tag — first scan after boot sees it, later scans miss; see project memory.)

## 7. First usage path — `tlsr-push` (2026-05-28)

`cd producer && uv run tlsr-push claude` (also `quote`) — the first real BLE-push entry point (`scripts/tlsr_push.py`). Discovers the tag, **skips entirely without connecting** when the built page's `data_ver` matches the tag's advertised one (the dedup payoff for a recurring cron), else connects + pushes. `--force` / `--address <uuid>`. HW-confirmed: claude pushed+rendered, re-run skipped no-connect.

**Arena limit (important):** `render/pipeline.c:78` hardcodes a **4096 B arena shared by parse + layout**. The full `build_claude_page`/`build_codex_page` (~25 nodes, designed for the big s3/x4 panels) **overflow it on the 250×128 tag** → `XFER_ERR_RENDER`. The golden harness uses the Python `preview_png` renderer (no arena) so never caught it. Fix = **device-appropriate compact layouts**: `build_claude_compact_page` (~5 nodes, 417 B) renders fine (verified via `host_test pipeline-e2e` → PIPE_OK). `tlsr-push` uses compact; **codex/overview still need compact builders**, and a firmware-pipeline golden test should guard the arena budget (closes the Python-only golden gap).
