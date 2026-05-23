# Skip-when-unchanged render + button-only navigation

The Spectra 6 panel on the M5PaperColor runs the same waveform regardless of
which M5GFX `epd_mode_t` we select — measured 16.7–16.8 s for `epd_quality`,
`epd_text`, `epd_fast`, and `epd_fastest`. There is no genuinely-faster mode
to opt into for rotation events. The only refresh-cost lever we actually have
is to **not refresh when nothing changed**.

## Behavior

1. **Skip when content unchanged.** Before drawing, compare the current page's
   `(id, data_ts)` against the most recent successfully-rendered tuple. If
   both match, log `[render] skip — id=… data_ts=… unchanged` and return
   without touching the panel. The image already on the panel is byte-equal
   to what we would draw, so the user sees no difference.

2. **Persist the marker across deep sleep.** Store `last_rendered_id` and
   `last_rendered_data_ts` in NVS so a wake-from-sleep can also skip. Avoid
   re-serializing the full pages blob (~15 KB) on every render — add a
   `nvs_save_render_marker()` partial write that touches only the two
   marker keys.

3. **Remove timer-driven rotation.** Timer wakes are scheduled refreshes only.
   `FETCH_INTERVAL_SEC = 1800` wakes the device every 30 min; BtnA/BtnB are the
   only page navigation path. If the current page's `(id, data_ts)` is unchanged
   after a scheduled fetch, the render skip keeps the panel untouched.

## Where skip actually helps

- **`r` re-render command** (dev console) → always skips after the first
  render of a page. Cuts the cost of debugging cycles in half.
- **Scheduled refresh with unchanged data** → the panel already shows the same
  image; skip saves
  one full refresh on every undisturbed wake.
- **Forced refetch (`f` / ButtonC) where producer didn't update**
  `data_ts` → current page content is identical; skip.
- **Button navigation** renders because the selected page id changes.

## Cross-session migration

First boot after flash: `last_rendered_id` defaults to `""` and
`last_rendered_data_ts` defaults to `0`. The compare against any real
`PageRecord` fails (id is non-empty), so the first render proceeds. The
"NOT_FOUND" log line from `Preferences.getString("rid", "")` on first boot
is expected and harmless.

## Status-bar drift

When skip fires, the status-bar fields (battery %, env, refresh HH:MM) on
the panel can be a few minutes stale relative to current sensor readings.
This is intrinsic to e-ink display freshness; the user already accepts that
the screen reflects the last refresh, not real-time data. We deliberately
do not factor battery / env / `last_fetch_s` into the skip comparison —
forcing a 16.8 s refresh to bump the battery indicator by 1 % is exactly
the kind of churn we are trying to avoid.

## Verification

- `pio test -e native` — 22 green.
- `pio run -e m5stack-papercolor` and `…-prod` — both build.
- On-device:
  - Boot: full render (no marker yet).
  - `r`: skip (marker matches).
  - `n`: full render (different id).
  - `r`: skip (marker matches new page).
- Cross-deep-sleep skip is covered by the NVS persistence; verified by code
  inspection (`nvs_load` reads `rid`/`rts`; `nvs_save_render_marker` writes
  them after every successful render).
