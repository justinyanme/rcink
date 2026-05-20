# tlsr-layout — Phase 8: Power Validation (Manual)

**Status:** Hardware-only — not codex-doable. This document is a checklist for a human with a flashed Phase-5 firmware, a CR2032, a shunt resistor (10 Ω), and an oscilloscope or low-current logger (e.g., Otii Arc, Joulescope, or a Power Profiler Kit II).

**Goal:** Confirm the tlsr-layout firmware hits the spec §9 power budget:

- PEEK_LOOP with no Mac: **≤ 30 µA mean** current.
- One transfer per hour: **2+ years on a CR2032** (~230 mAh, derated to ~180 mAh in practice).

If the measured numbers miss the budget, the most likely culprits are documented at the bottom with mitigation suggestions.

---

## Setup

1. Flash `firmware/build/src/TLSR-LAYOUT.bin` to a Blozi 2.13" BWR FPC-A002 or FPC-7528B board (the tlsr-price-tag-compatible hardware).
2. Solder a 10 Ω resistor in series with the battery positive terminal. Connect scope / logger across it.
3. Verify the board boots: EPD shows the time scene (left over from the price-tag scaffold; replaced in Phase 4 by the rendered DSL page).
4. Run `tlsr-layout devices --duration 30` from the Mac. Confirm one entry shows up with `data_ver=0x00000000` and the right `battery_mV` (within 50 mV of the CR2032's actual voltage).

## Test A — Idle PEEK loop (no Mac in range)

1. Power-cycle the tag with the Mac out of range or BLE off.
2. After the 5-minute sync window, the tag should fall into PEEK_LOOP (60 s deep sleep + ~5 s advertise).
3. Capture 10 minutes of current. Compute the **mean**.

**Pass:** mean ≤ 30 µA.
**Fail:** see "Common power problems" below.

## Test B — One transfer per hour

1. Mac runs `tlsr-layout serve` with the rotation `--page example` (one page, so the producer pushes once per data_ver change — which it isn't, so we trigger a manual push).
2. Force a transfer every 60 minutes: a launchd job that runs `tlsr-layout push example --device <MAC>` on the hour.
3. Capture 24 hours of current. Compute the **mean**.

**Pass:** mean × 24 × 365 / (180 mAh × 1000 µA/mA) ≥ 2 years.
   Equivalently: **mean ≤ 10 µA** averaged over the day (allowing slack vs the 30 µA peek-only number since the spend is concentrated in the brief transfer windows).
**Fail:** the EPD refresh + radio-on window dominates; tune in spec §7.2 — increase `TLSR_LAYOUT_PEEK_INTERVAL_S` or implement the adaptive 300 s extension (§7.2 "stretch").

## Test C — Single-transfer energy

1. Trigger a single push with the scope already capturing.
2. Measure: peak current, total mAs (charge) spent during the transfer.

**Reference numbers (price-tag baseline):**
- Peak: ~12 mA during BLE TX.
- EPD refresh: ~20 mA peak, ~3 s.
- Total per transfer: ~30–50 mAs.

**Pass:** total ≤ 60 mAs.
**Fail:** investigate `epd_213.c` refresh sequence — full refresh is wasteful when only part of the panel changes. The render pipeline could be enhanced with partial-refresh support in a later phase.

---

## Common power problems

| Symptom | Likely cause | Mitigation |
|---------|--------------|------------|
| Mean > 100 µA at idle | Deep sleep not entered — main loop spinning | Check `app.c` calls `cpu_sleep_wakeup(DEEPSLEEP_MODE_RET_SRAM_LOW32K, …)` between PEEK windows |
| Mean ~3 mA at idle | BLE stack staying connected | Check the 120 s idle disconnect timer in `ble.c` is still firing |
| Spikes every 60 s but mean still high | Advertise window too long | Reduce `TLSR_LAYOUT_ADV_WINDOW_S` to 3 |
| Battery drained in days, not years | Mac scanning aggressively keeps tag in connection | Reduce serve loop scan duty cycle (Phase 7 enhancement) |
| EPD refresh > 4 s | Wrong panel selected | Verify `EPD_PAD_TOP` matches the FPC variant (FPC-A002 = 0, FPC-7528B = 8) |

---

## Reporting

After running the three tests, append a `docs/power-report-YYYYMMDD.md` with:

- Mean idle current (Test A) and capture screenshot.
- 24 h mean (Test B) and capture screenshot.
- Single-transfer profile (Test C) with annotated peaks.
- Predicted battery life given the measured numbers.

Commit this report — it's the ship-or-not-ship artifact for the project.

If both numbers pass, tag `tlsr-layout-v1` and write a short release note pointing at the spec and each phase's tag.
