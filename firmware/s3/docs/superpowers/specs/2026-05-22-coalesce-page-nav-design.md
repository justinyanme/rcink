# Coalesce rapid page-nav taps + per-press LED feedback

A single e-ink refresh on the Spectra 6 panel runs ~16.8 s and cannot be
shortened. So tapping BtnA three times to advance three pages currently
costs ~50 s of wall-clock, one render at a time. The user usually knows
the net direction within the first second of tapping — they want the
device to wait briefly, sum up taps, and emit **one** render at the net
target.

## Behavior

1. **Wake from ext1 button** (existing): `current_wake_reason()` returns
   `ButtonA` / `ButtonB` / `ButtonC`.

2. **Coalesce only nav buttons (A/B).** If wake reason is `ButtonA` or
   `ButtonB`, seed `nav_delta` with `+1` (A = next) or `-1` (B = prev),
   flash the LED for that initial press, then enter a coalescing loop:

   - **Sliding deadline**: `deadline = now + 500 ms`. Every additional
     press extends `deadline` by another 500 ms.
   - **Hard cap**: never wait more than 2000 ms from the first wake-press.
     Protects against stuck buttons / hardware bounce.
   - **Poll cadence**: `M5.update()` + `wasPressed()` checks every 10 ms.
   - **BtnA press** → `nav_delta++`, flash green, extend deadline.
   - **BtnB press** → `nav_delta--`, flash blue, extend deadline.
   - **BtnC press** → flash amber, **break out** of the loop and set
     `force_refetch = true`. BtnC wins over pending nav.

3. **BtnC wake** (refetch): no coalescing loop. Existing refetch path
   runs immediately. Rationale: refetch is heavy (~5–10 s of WiFi + NTP
   + HTTPS), there is no sensible "net" of refetches, and the user
   pressed BtnC deliberately.

4. **Timer / PowerOn / Unknown wake**: no coalescing — proceeds as today.

5. **Apply net delta** to `page_index` using modulo with sign-safe wrap:
   `page_index = ((page_index + nav_delta) % n + n) % n`. Persist via the
   existing `nvs_save_progress(page_index)` once, after the loop.

6. **If `force_refetch`**: run `refetch_now()` (handles its own WiFi +
   NTP + fetch + LED states), then render the resulting page.

7. **Render** the final page (one panel refresh). Deep sleep.

## LED feedback

Each press triggers an 80 ms full-brightness overlay that visually
preempts the current heartbeat without altering it:

- **BtnA (next)** → green `(0, 96, 0)`
- **BtnB (prev)** → blue `(0, 0, 96)`
- **BtnC (refetch)** → amber `(96, 56, 0)`

Both onboard RGB LEDs flash simultaneously for maximum visibility. After
80 ms, the led_task resumes the prior pattern (Launch / Refreshing /
etc.) on its own — no state transition needed.

The first flash also doubles as the user's cue that the device has
finished booting and is now listening for additional taps. Taps that
arrive during the ~250–400 ms boot window before the coalescing loop
starts are not detectable (ext1 only captures the single wake transition;
the GPIO state has stabilized by the time M5.update() runs). This is an
accepted limitation — the LED flash trains the cadence.

## Components touched

### `src/status/led_status.{h,cpp}` — add pulse overlay

New API:

```cpp
void led_status_pulse(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms);
```

Implementation: store `g_overlay_color` + `g_overlay_until_ms` under the
existing critical-section lock. The `led_task` loop checks `millis() <
g_overlay_until_ms` first; if true, drive both LEDs to the overlay color
solid (skip the heartbeat math); else use the existing
`pattern_for(state)` path. No new `LedState` enum entry.

The pulse must run synchronously the moment it is requested even when
the led_task is between its 20 ms vTaskDelay ticks — call
`write_pattern`-style code directly in `led_status_pulse` to push the
color to the strip immediately, then the task takes over for the
remainder of the overlay window.

### `src/main.cpp` — replace single-step nav with coalesced delta

Inside `setup()`, after `g_state = nvs_load();` and before the existing
`if (wr == WakeReason::ButtonA) ... else if (wr == WakeReason::ButtonB) ...`
block, run a `coalesce_nav_presses()` helper that:

- Seeds `nav_delta` and flashes the wake press's color.
- Loops with `M5.update()` polling until deadline.
- Returns `{nav_delta, force_refetch}`.

Then:

- If `force_refetch`, call `refetch_now()` (sets `needs_fetch` semantics).
- Apply `nav_delta` to `g_state.page_index` with sign-safe modulo.
- Persist progress if changed (existing `nvs_save_progress` call).
- `render_current()` and sleep (existing tail).

The `needs_fetch` decision earlier in `setup()` does not need to change —
it still runs for `Timer`, `ButtonC`, and empty-pages cases. A BtnC
pressed *during* coalescing turns into `force_refetch = true` and we
call `refetch_now()` explicitly after the loop, which is the same path.

Estimated `setup()` growth: ~25–30 LOC including the helper inline in
the anonymous namespace.

### `src/dev/console.cpp` (no change needed)

The dev console's `n` / `b` commands invoke single-step nav directly and
do not run through the coalescing loop. That's fine — dev console is
local interactive testing where coalescing has no value.

## Constants

Hard-code in `main.cpp` (anonymous namespace) — these are tuning knobs,
not user-facing config:

```cpp
constexpr uint16_t kNavCoalesceWindowMs = 500;
constexpr uint16_t kNavCoalesceMaxMs    = 2000;
constexpr uint16_t kNavPollIntervalMs   = 10;
constexpr uint16_t kNavLedPulseMs       = 80;
```

## README updates

- "Buttons" section: note that rapid taps within 500 ms coalesce into a
  single net page jump (e.g. A×3 then B×1 advances 2 pages with one
  refresh).
- "LED states" section: add the three press-pulse colors.

## Out of scope

- Faster e-ink refresh modes — already proven equal on this panel
  (see `2026-05-20-render-skip-design.md`).
- Coalescing across deep-sleep cycles — wakes are independent; a tap
  that fails to wake the device because the prior wake is still mid-render
  is not a problem this design solves.
- Replacing ext1 wake with EXT0 + ISR for sub-ms latency.
- Battery/env updates during the coalescing window — kept as a single
  pre-loop read, same as today.

## Verification

- `pio test -e native` — must stay green; no native test depends on the
  coalescing code path.
- `pio run -e m5stack-papercolor` and `…-prod` — both build.
- On-device script (manual):
  1. Boot device, wait for idle.
  2. Tap BtnA three times quickly (each within 500 ms of the last) —
     observe three green flashes, then one render, page advances by 3.
  3. Tap BtnA twice then BtnB once quickly — observe two green + one
     blue flash, then one render, page advances by 1.
  4. Tap BtnA, then wait 1 s, then tap BtnA again — observe two
     separate render cycles (window closed between taps).
  5. Tap BtnA twice then BtnC during the window — observe two green +
     one amber flash, refetch runs, then render of the (possibly
     unchanged) current page.
  6. Hold BtnA continuously for 1 s — exactly one green flash, page
     advances by 1 (M5Unified debouncing prevents multi-count).
