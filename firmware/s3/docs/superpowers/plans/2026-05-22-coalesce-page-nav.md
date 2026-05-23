# Page-Nav Coalescing + LED Press Feedback — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Coalesce rapid BtnA/BtnB taps within a 500 ms sliding window (2 s cap) into one net page-index change with a single e-ink refresh, and flash the RGB LEDs once per press for tactile feedback.

**Architecture:** A short polling loop runs in `setup()` after wake-reason detection on `ButtonA`/`ButtonB` wakes. It accumulates `+1`/`-1` deltas and extends a sliding deadline on each new press. The LED driver gets a one-shot pulse overlay that preempts the heartbeat pattern for ~80 ms without changing `LedState`. BtnC pressed mid-window cancels nav and triggers refetch.

**Tech Stack:** C++17 (PlatformIO + M5Unified/M5GFX/M5PM1), Unity for native-testable pure-logic helpers, on-device manual verification for time/IO-dependent paths.

**Spec:** `docs/superpowers/specs/2026-05-22-coalesce-page-nav-design.md`.

---

## File map

| File | Action | Purpose |
|---|---|---|
| `src/power/nav_delta.h` | create | Pure-logic sign-safe modulo helper for applying coalesced delta to `page_index` |
| `test/test_native_nav_delta/test_main.cpp` | create | Unity tests for `apply_nav_delta` |
| `platformio.ini` | modify | extend `build_src_filter` for native env to include `+<power/>` so the header-only `nav_delta.h` is reachable (no .cpp needed) |
| `src/status/led_status.h` | modify | add `led_status_pulse(r, g, b, duration_ms)` |
| `src/status/led_status.cpp` | modify | implement overlay state + early-return in task loop + immediate synchronous write |
| `src/main.cpp` | modify | add `coalesce_nav_presses()` helper in anonymous namespace; replace single-step nav with delta application |
| `README.md` | modify | document coalescing in Buttons section + press colors in LED states section |

No changes to `src/dev/console.{h,cpp}` — the dev console's `n`/`b` commands remain single-step.

---

## Phase 1 — Sign-safe nav-delta helper (TDD, native-testable)

### Task 1: Failing test for `apply_nav_delta`

**Files:**
- Create: `test/test_native_nav_delta/test_main.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <unity.h>
#include "power/nav_delta.h"

using rcpaper::apply_nav_delta;

void test_zero_delta_is_identity() {
  TEST_ASSERT_EQUAL_UINT8(0, apply_nav_delta(0, 0, 5));
  TEST_ASSERT_EQUAL_UINT8(3, apply_nav_delta(3, 0, 5));
}

void test_positive_delta_wraps_forward() {
  // 5 pages, start at 3, +3 → 6 mod 5 = 1
  TEST_ASSERT_EQUAL_UINT8(1, apply_nav_delta(3, 3, 5));
  // +5 = full lap → same
  TEST_ASSERT_EQUAL_UINT8(2, apply_nav_delta(2, 5, 5));
}

void test_negative_delta_wraps_backward() {
  // 5 pages, start at 1, -3 → -2 → wraps to 3
  TEST_ASSERT_EQUAL_UINT8(3, apply_nav_delta(1, -3, 5));
  // -1 from 0 → wraps to 4
  TEST_ASSERT_EQUAL_UINT8(4, apply_nav_delta(0, -1, 5));
}

void test_mixed_taps_net_result() {
  // Three forward + one back from page 2 in a 7-page set = net +2 → page 4.
  TEST_ASSERT_EQUAL_UINT8(4, apply_nav_delta(2, 2, 7));
  // Three back + one forward from page 1 in a 7-page set = net -2 → page 6.
  TEST_ASSERT_EQUAL_UINT8(6, apply_nav_delta(1, -2, 7));
}

void test_single_page_clamps_to_zero() {
  // Degenerate but legal: one-page state. Any delta returns 0.
  TEST_ASSERT_EQUAL_UINT8(0, apply_nav_delta(0, 5, 1));
  TEST_ASSERT_EQUAL_UINT8(0, apply_nav_delta(0, -7, 1));
}

void test_large_positive_delta() {
  // Hard-cap behavior is enforced by the caller, but the helper itself
  // must still return the correct modulo even for big inputs.
  TEST_ASSERT_EQUAL_UINT8(2, apply_nav_delta(0, 17, 5));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_zero_delta_is_identity);
  RUN_TEST(test_positive_delta_wraps_forward);
  RUN_TEST(test_negative_delta_wraps_backward);
  RUN_TEST(test_mixed_taps_net_result);
  RUN_TEST(test_single_page_clamps_to_zero);
  RUN_TEST(test_large_positive_delta);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails to compile**

Run: `pio test -e native -f test_native_nav_delta`
Expected: BUILD ERROR — `power/nav_delta.h: No such file`.

### Task 2: Create the header

**Files:**
- Create: `src/power/nav_delta.h`
- Modify: `platformio.ini`

- [ ] **Step 1: Add the helper**

```cpp
// src/power/nav_delta.h
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rcpaper {

// Apply a signed delta to a page index with sign-safe modulo wrap.
// `n` must be >= 1 (callers guard against empty page lists upstream).
inline uint8_t apply_nav_delta(uint8_t current, int delta, size_t n) {
  if (n == 0) return 0;
  // Promote to long long so big deltas don't overflow before modulo.
  long long wide = (long long)current + (long long)delta;
  long long m = (long long)n;
  long long r = wide % m;
  if (r < 0) r += m;
  return (uint8_t)r;
}

}  // namespace rcpaper
```

- [ ] **Step 2: Extend native build_src_filter**

Edit `platformio.ini`, the `[env:native]` section. Change:

```ini
build_src_filter = -<main.cpp> +<render/> +<proto/> +<sense/>
```

to:

```ini
build_src_filter = -<main.cpp> +<render/> +<proto/> +<sense/> +<power/>
```

(The header is inline so no .cpp is needed; the filter widening just keeps things consistent if power/ ever grows .cpp files that are pure-logic.)

- [ ] **Step 3: Run the test to verify it passes**

Run: `pio test -e native -f test_native_nav_delta`
Expected: 6 tests, all PASS.

- [ ] **Step 4: Run the full native suite to confirm nothing regressed**

Run: `pio test -e native`
Expected: all pre-existing tests + the 6 new ones, all green.

- [ ] **Step 5: Commit**

```bash
git add src/power/nav_delta.h test/test_native_nav_delta platformio.ini
git commit -m "feat(power): sign-safe nav-delta modulo helper + native tests"
```

---

## Phase 2 — LED pulse overlay

### Task 3: Extend `led_status` API

**Files:**
- Modify: `src/status/led_status.h`

- [ ] **Step 1: Add the pulse declaration**

Insert after the existing `void led_status_show(LedState state);` line:

```cpp
// Briefly override the current pattern with a solid color on both LEDs for
// `duration_ms`. The overlay does NOT alter the active LedState — the
// heartbeat pattern resumes automatically when the overlay window expires.
// Safe to call from setup()/loop() at any time; cheap (~one RMT write).
void led_status_pulse(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms);
```

### Task 4: Implement the pulse overlay

**Files:**
- Modify: `src/status/led_status.cpp`

- [ ] **Step 1: Add overlay state under the existing `namespace { ... }`**

After the line `LedState g_state = LedState::Off;` add:

```cpp
Rgb      g_overlay_rgb{0, 0, 0};
uint32_t g_overlay_until_ms = 0;
```

- [ ] **Step 2: Make the task respect the overlay**

In `led_task`, replace the body of the `for (;;)` loop. The new body must read overlay + state under the lock, then if `millis() < overlay_until` drive both LEDs to the overlay color solid (skip the heartbeat math), else compute the heartbeat as today.

Replace the existing loop body (from `LedState state;` through `vTaskDelay(...)`) with:

```cpp
LedState state;
uint32_t started_ms;
Rgb      overlay_rgb;
uint32_t overlay_until;
portENTER_CRITICAL(&g_lock);
state         = g_state;
started_ms    = g_state_start_ms;
overlay_rgb   = g_overlay_rgb;
overlay_until = g_overlay_until_ms;
portEXIT_CRITICAL(&g_lock);

uint32_t now = millis();
bool overlay_active = (int32_t)(overlay_until - now) > 0;

if (overlay_active) {
  if (first || !last_overlay_active || overlay_rgb.r != last_overlay_rgb.r ||
      overlay_rgb.g != last_overlay_rgb.g || overlay_rgb.b != last_overlay_rgb.b) {
    rmt_data_t led_data[kRgbLedCount * 24];
    rmt_data_t* out = led_data;
    for (size_t i = 0; i < kRgbLedCount; ++i) write_pixel(out, overlay_rgb);
    if (g_ready && g_rmt != nullptr) {
      rmtWriteBlocking(g_rmt, led_data, kRgbLedCount * 24);
    }
    last_overlay_active = true;
    last_overlay_rgb    = overlay_rgb;
    first               = false;
    // Force a heartbeat redraw when the overlay clears.
    last_state          = LedState::Off;
    last_on             = false;
  }
} else {
  const Pattern p = pattern_for(state);
  bool on = false;
  if (p.period_ms > 0 && p.on_ms > 0) {
    on = (now - started_ms) % p.period_ms < p.on_ms;
  }
  if (first || last_overlay_active || state != last_state || on != last_on) {
    write_pattern(p, on);
    first               = false;
    last_state          = state;
    last_on             = on;
    last_overlay_active = false;
  }
}

vTaskDelay(pdMS_TO_TICKS(20));
```

Also add the two new locals at the top of `led_task`, alongside the existing `last_state` / `last_on` / `first`:

```cpp
bool last_overlay_active = false;
Rgb  last_overlay_rgb{0, 0, 0};
```

- [ ] **Step 3: Implement `led_status_pulse`**

Add after `void led_status_show(LedState state) { ... }`:

```cpp
void led_status_pulse(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms) {
  if (duration_ms == 0) return;
  Rgb color{r, g, b};
  uint32_t until = millis() + duration_ms;
  portENTER_CRITICAL(&g_lock);
  g_overlay_rgb      = color;
  g_overlay_until_ms = until;
  portEXIT_CRITICAL(&g_lock);

  // Push the color immediately so the user sees the flash even if the
  // led_task is mid-vTaskDelay. The task takes over for the remainder of
  // the overlay window.
  if (g_ready && g_rmt != nullptr) {
    rmt_data_t led_data[kRgbLedCount * 24];
    rmt_data_t* out = led_data;
    for (size_t i = 0; i < kRgbLedCount; ++i) write_pixel(out, color);
    rmtWriteBlocking(g_rmt, led_data, kRgbLedCount * 24);
  }
}
```

- [ ] **Step 4: Build for device to confirm no link errors**

Run: `pio run -e m5stack-papercolor`
Expected: builds cleanly. No warnings about `last_overlay_active` shadowing.

- [ ] **Step 5: Run native tests to confirm no regressions**

Run: `pio test -e native`
Expected: all green (led_status not in native filter, so untouched).

- [ ] **Step 6: Commit**

```bash
git add src/status/led_status.h src/status/led_status.cpp
git commit -m "feat(led): one-shot pulse overlay that preempts heartbeat"
```

---

## Phase 3 — Coalescing loop in `setup()`

### Task 5: Add the coalescing helper

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add the constants + helper in the existing `namespace rcpaper { ... }`**

Find the line `static EnvReading     g_env{};` near the top of the file. Below the two `g_battery` / `g_env` declarations, insert:

```cpp
// ---- nav-press coalescing ------------------------------------------------
//
// On a ButtonA/ButtonB wake we count taps for 500 ms (sliding, max 2 s) and
// apply the net delta in one render. BtnC during the window cancels nav and
// requests a refetch — see coalesce_nav_presses() below and the apply step
// further down in setup(). Boot delay (~250-400 ms from ext1 wake) is
// outside the window: taps that arrive before led_status_init() are lost.
// The first LED flash tells the user "I'm listening, keep tapping."

constexpr uint16_t kNavCoalesceWindowMs = 500;
constexpr uint16_t kNavCoalesceMaxMs    = 2000;
constexpr uint16_t kNavPollIntervalMs   = 10;
constexpr uint16_t kNavLedPulseMs       = 80;

struct NavCoalesceResult {
  int  delta         = 0;
  bool force_refetch = false;
};

static void flash_for_button(WakeReason r) {
  switch (r) {
    case WakeReason::ButtonA: led_status_pulse(0,  96, 0,  kNavLedPulseMs); break;
    case WakeReason::ButtonB: led_status_pulse(0,  0,  96, kNavLedPulseMs); break;
    case WakeReason::ButtonC: led_status_pulse(96, 56, 0,  kNavLedPulseMs); break;
    default: break;
  }
}

// Caller must only invoke this for WakeReason::ButtonA or ButtonB.
static NavCoalesceResult coalesce_nav_presses(WakeReason initial) {
  NavCoalesceResult out;
  out.delta = (initial == WakeReason::ButtonA) ? 1 : -1;
  flash_for_button(initial);

  const uint32_t start_ms = millis();
  uint32_t deadline_ms    = start_ms + kNavCoalesceWindowMs;

  for (;;) {
    uint32_t now = millis();
    if ((int32_t)(deadline_ms - now) <= 0) break;
    if (now - start_ms >= kNavCoalesceMaxMs) break;

    M5.update();
    if (M5.BtnA.wasPressed()) {
      out.delta++;
      led_status_pulse(0, 96, 0, kNavLedPulseMs);
      deadline_ms = now + kNavCoalesceWindowMs;
    } else if (M5.BtnB.wasPressed()) {
      out.delta--;
      led_status_pulse(0, 0, 96, kNavLedPulseMs);
      deadline_ms = now + kNavCoalesceWindowMs;
    } else if (M5.BtnC.wasPressed()) {
      out.force_refetch = true;
      led_status_pulse(96, 56, 0, kNavLedPulseMs);
      break;
    }
    delay(kNavPollIntervalMs);
  }
  Serial.printf("[nav] coalesce done delta=%d force_refetch=%d window=%u ms\n",
                out.delta, (int)out.force_refetch,
                (unsigned)(millis() - start_ms));
  return out;
}
```

Also add the `#include "power/nav_delta.h"` line with the other `#include` directives at the top of `main.cpp`. Put it next to the other `power/` include:

```cpp
#include "power/nav_delta.h"
#include "power/sleep.h"
```

- [ ] **Step 2: Build for device to confirm the helper compiles**

Run: `pio run -e m5stack-papercolor`
Expected: builds cleanly.

### Task 6: Wire the helper into setup()

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Replace the single-step nav block**

Locate this existing block in `setup()` (lines ~230-242):

```cpp
  // Page navigation is button-only. Timer wakes are scheduled refreshes and
  // must not advance the page; if fetched content is unchanged, render_current()
  // will skip the panel refresh via the persisted render marker.
  if (!g_state.pages.empty()) {
    uint8_t prev_page_index = g_state.page_index;
    if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;
    if (wr == WakeReason::ButtonA) {
      g_state.page_index = (uint8_t)((g_state.page_index + 1) % g_state.pages.size());
    } else if (wr == WakeReason::ButtonB) {
      g_state.page_index = (uint8_t)((g_state.page_index + g_state.pages.size() - 1)
                                      % g_state.pages.size());
    }
    if (g_state.page_index != prev_page_index) {
      nvs_save_progress(g_state.page_index);
    }
  }
```

Replace with:

```cpp
  // Page navigation is button-only. Timer wakes are scheduled refreshes and
  // must not advance the page; on a nav wake we coalesce rapid taps within a
  // sliding 500 ms window (cap 2 s) into a single net delta + one render.
  // BtnC pressed during coalescing cancels nav and forces a refetch.
  if (!g_state.pages.empty()) {
    uint8_t prev_page_index = g_state.page_index;
    if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;

    if (wr == WakeReason::ButtonA || wr == WakeReason::ButtonB) {
      NavCoalesceResult nav = coalesce_nav_presses(wr);
      if (nav.force_refetch) {
        refetch_now();
      }
      if (nav.delta != 0) {
        g_state.page_index = apply_nav_delta(g_state.page_index, nav.delta,
                                             g_state.pages.size());
      }
    }
    if (g_state.page_index != prev_page_index) {
      nvs_save_progress(g_state.page_index);
    }
  }
```

- [ ] **Step 2: Build for device**

Run: `pio run -e m5stack-papercolor && pio run -e m5stack-papercolor-prod`
Expected: both build cleanly.

- [ ] **Step 3: Run native tests to confirm no regressions**

Run: `pio test -e native`
Expected: all green (main.cpp not in native filter).

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(nav): coalesce rapid BtnA/BtnB taps with LED feedback"
```

---

## Phase 4 — Docs

### Task 7: README updates

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update the LED states section**

In the bullet list under `## LED states`, replace the existing list with:

```markdown
- white heartbeat — launch
- blue heartbeat — connecting WiFi
- green heartbeat — WiFi connected
- red heartbeat — WiFi failed
- cyan/blue heartbeat — NTP or Axiom sync
- amber/red heartbeat — sync/fetch failed, cache kept
- amber heartbeat — panel refresh/render
- short green flash — BtnA press registered (next-page)
- short blue flash — BtnB press registered (prev-page)
- short amber flash — BtnC press registered (refetch)
- idle states — LEDs off
- off — entering deep sleep
```

- [ ] **Step 2: Update the Buttons section**

Replace the existing `## Buttons` section body with:

```markdown
- **BtnA** — next page
- **BtnB** — previous page
- **BtnC** — force re-fetch from Axiom now

Rapid taps within ~500 ms of each other coalesce into a single net page
jump and one e-ink refresh. Example: BtnA × 3 then BtnB × 1 quickly →
device advances 2 pages with one render (≈ 17 s) instead of four.
The window slides on each new tap, caps at 2 s total, and a BtnC press
during the window cancels nav and triggers a refetch instead. Each press
flashes the LEDs in its color so you know it registered.
```

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: document nav coalescing + per-press LED flash"
```

---

## Phase 5 — On-device verification

These are user-driven manual checks. The plan-runner should pause here and ask the user to flash a dev build and walk through the script.

### Task 8: Flash + run the manual script

**Files:** none

- [ ] **Step 1: Flash the production env**

Run: `pio run -e m5stack-papercolor-prod -t upload`
Expected: flash succeeds; device reboots into the full deep-sleep loop.

- [ ] **Step 2: Run the verification script**

For each case below, the user observes LED + final page. The exact starting page index doesn't matter — only the relative motion.

1. **Single BtnA** — one green flash, page advances by 1 with one render.
2. **BtnA × 3 quickly (≤ 400 ms gaps)** — three green flashes, page advances by 3 with one render.
3. **BtnA × 2 then BtnB × 1 quickly** — two green + one blue flash, page advances by 1 with one render.
4. **BtnA then wait ~1 s then BtnA** — observed as two separate wake cycles (two renders). The first wake's window closes between presses; the second tap re-wakes from deep sleep.
5. **BtnA × 2 then BtnC during the window** — two green flashes, one amber flash, refetch runs (WiFi LED activity), then a render of the (possibly unchanged) current page.
6. **Hold BtnA continuously for 1 s** — exactly one green flash, page advances by 1. Confirms M5Unified debouncing prevents multi-count from a held button.
7. **Serial log inspection** — for each case, the line `[nav] coalesce done delta=<n> force_refetch=<0|1> window=<ms> ms` is present with the expected `delta` and `force_refetch` values.

- [ ] **Step 3: Note any deviation**

If any case mismatches expectations, capture the serial log of that wake cycle and append it to a debugging note in this plan for the next iteration. Otherwise mark Phase 5 complete and close out.

---

## Self-review

- **Spec coverage**: §Behavior steps 1-7 → Tasks 5-6. §LED feedback → Tasks 3-4 + flash_for_button. §Components touched → File map. §Constants → Task 5. §README updates → Task 7. §Verification → Task 8.
- **Placeholders**: none — every step contains literal code, file paths, and commands.
- **Type consistency**: `NavCoalesceResult` defined once in Task 5, consumed in Task 6 with matching fields (`delta`, `force_refetch`). `apply_nav_delta(uint8_t, int, size_t)` signature is identical in Task 1 (test), Task 2 (header), and Task 6 (call site). `led_status_pulse(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms)` signature matches across Tasks 3, 4, 5.
- **Order**: Task 5 introduces both `coalesce_nav_presses` and `flash_for_button`, then Task 6 calls them — no forward references at edit time.
