# Bucket A — bug + correctness fixes

Reviewing the rcpapercolor codebase surfaced five small correctness gaps. This
spec covers the four worth fixing; the fifth (wifi lifecycle) is documented as
intentional and gets only a comment.

Bucket A is the first of four refactor buckets agreed in conversation; B
(producer dedup), C (split `main.cpp`), and D (delete unused) follow in
separate specs.

## A1 — drop unused `max_lines` field

`TextNode::max_lines` is parsed (`src/proto/parser.cpp:87`) and stored on the
schema, but the renderer never reads it. `draw_text` in
`src/render/primitives.cpp` always single-line truncates via `fit_utf8` — which
matches the producer's intent (calendar event titles, todo rows). The field is
a no-op masquerading as a feature.

Following the project's "no backward compatibility" rule, the field is removed
end-to-end:

- `src/proto/schema.h::TextNode` — drop `max_lines` member.
- `src/proto/parser.cpp` — drop the `max_lines` parse line.
- `producer/rcpaper/layout.py::text()` — drop the `max_lines` kwarg.
- `producer/rcpaper/pages/calendar.py:97,99,126`,
  `producer/rcpaper/pages/todo.py:89,95,113` — drop `max_lines=1` args.
- Any test asserting on `max_lines` in the layout dict — drop the assertion.

No pixel change. Tests verify: layout dicts no longer contain a `max_lines`
key, and native engine tests still pass.

## A2 — `paint_status_bar` width guard

`src/main.cpp:131-212` paints five groups (battery, temp, humidity, data time,
refresh time) by incrementing a cursor `x` with no bounds check. On the current
540-px panel with current strings the bar fits; any string growth (3-digit temp
in unusual cases, longer locale) silently clips past the right edge.

Fix: before each non-leading group, check that the group's expected width fits
in `W - x - right_margin`. If not, stop drawing the rest of the bar and log
once with the actual usage. The earlier groups (battery + temp) always fit and
are unconditional; the optional tail groups (humidity, data time, refresh time)
gain the guard.

Implementation: a small `try_draw_group(canvas, &x, W, paint_fn)` helper that
returns `bool fit`; on `false` it logs `[status] overflow at x=N W=M` and the
caller `break`s out of the chain.

Verified manually on-device after change; no native test (status bar is
device-only).

## A3 — log past-due deep-sleep delta

`src/power/sleep.cpp:38` clamps `delta = epoch_seconds - now` to a 10-s floor.
If `last_fetch_s` is corrupt or NTP delivered a future time, the device enters
a tight 10-s wake loop with no diagnostic.

Fix: when the unclamped delta is < 10, emit one `Serial.printf` showing the
requested target, current `time(nullptr)`, and the chosen floor before
clamping. Keep the floor — sub-10s deep sleep isn't useful, and crashing here
would brick the device.

No test (sleep is device-only, side-effecting).

## A4 — drop `ttl_sec` from producer ingest

`producer/rcpaper/axiom.py::ingest()` writes `ttl_sec` to Axiom; nothing reads
it. The firmware computes staleness client-side from `last_fetch_s +
FETCH_INTERVAL_SEC` (`src/main.cpp:464`), and the producer's `data_ts` is
already on every event for any document-side use.

Fix: remove the `ttl_sec` kwarg from `ingest()`, remove the field from the
Axiom event payload, and remove the two callers passing it
(`scripts/quote_fetch.py:24`, `scripts/image_fetch.py:44`). Old events in
Axiom keep their `ttl_sec` column harmlessly until they age out; no migration.

Producer tests adjust to no longer assert on `ttl_sec` in the event payload.

## A5 — wifi lifecycle: comment, not a fix

On a second pass, the lifecycle is correct:

- `refetch_now` connects and leaves wifi up so `render_current` can pull
  remote images on the same wake.
- `render_current` reconnects only if `refetch_now` didn't run and a page
  needs a remote image (the `connected_here` flag).
- The setup epilogue at `src/main.cpp:687-689` is the single cleanup site.
- The dev-console `'f'` command (`src/main.cpp:570`) reproduces the same
  pattern for parity with the deep-sleep cycle.

Two connect sites, one disconnect site, by design. Add a one-paragraph comment
above `refetch_now` explaining the contract so future readers don't try to
"fix" it.

## Out of scope

- Bucket B–D (separate specs).
- `apply_local_tz()` redundancy in `format_local_hhmm` — polish, deferred to
  bucket B's surrounding cleanup if it touches that path.

## Verification

Before commit:

1. `cd producer && uv run pytest` — all green.
2. `pio test -e native` — all green.
3. `pio run -e m5stack-papercolor` — builds.
4. Manual on-device check: status bar renders normally on the current pages
   (no overflow trigger expected with current strings).
