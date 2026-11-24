# rcink rlcd

Firmware target for the **Waveshare ESP32-S3-RLCD-4.2** board — a desk-climate
dashboard.

- ESP32-S3-PICO-1 on the board in hand, 8 MB flash, 8 MB PSRAM
- **ST7305 4.2" monochrome reflective LCD** (e-ink-like), driven landscape
  **400×300, 1 bpp**
- Onboard **SHTC3** temp/humidity (I2C 0x70), **PCF85063** RTC (I2C 0x51),
  18650 battery ADC (GPIO4, 3:1 divider)
- Two firmware-readable side buttons: **BOOT** (GPIO0) and **KEY** (GPIO18).
  The physical **PWR** key is handled by the board power circuit, not by the app.
- USB-CDC console on `/dev/cu.usbmodem*`

## What it shows

Boots into a hand-drawn **dashboard** modelled on Waveshare's reference UI,
redesigned for the 400×300 landscape panel:

```
RCINK                         14:32
DESK CLIMATE             WED  JUN 12
────────────────────────────────────
            TEMPERATURE
              28.4 °C
HUMIDITY                        64%
[██████████████░░░░░░░░░░░░░░░░░░░░]
────────────────────────────────────
BATTERY  [████████░░░]  4.01V    87%
```

The climate comes from the onboard SHTC3; the clock from the PCF85063 RTC
(NTP re-syncs it whenever Wi-Fi is up), so the dashboard is correct **offline**.
The **Deck View** shows the Axiom page-deck ported from `firmware/t-amoled`.

### How it draws

The render pipeline is reused wholesale from `firmware/t-amoled`: the scene is
composed into a 16-bit `TFT_eSprite` (black ink on white). At push time the
sprite is thresholded to 1 bpp and packed into the ST7305's native buffer
(`src/render/mono_pack.cpp`, `src/display/st7305.cpp`) — there is **no** AMOLED
white↔black swap; reflective panels are read black-on-white directly. TFT_eSPI
is used only as a sprite/font engine (its ILI9341 setup is never driven against
real hardware). CJK in the Deck View renders via U8g2 + a baked WenQuanYi GB2312
font.

Both builds boot in light polarity (`RCRLCD_DEFAULT_DARK_MODE=0`): black ink
on a white background, exactly as rendered. Setting the flag to `1` inverts the
default packed 1 bpp frame for white-on-black. Hold **BOOT** (≥ 600 ms) or use
console `i` to toggle light/dark polarity live; the preference persists in NVS.
The default production environment retains the last ST7305 frame during deep
sleep. `waveshare-s3-rlcd-dev` uses the same state machine but never sleeps, for
recovery and development.

## Runtime modes

Dashboard, Deck, and error screens carry one truthful mode badge:

| Badge | Meaning | Behavior |
|---|---|---|
| `HOST` | USB **data host** detected from 1 ms SOF frames | stays awake; USB console + both buttons available; usage fetch every 5 minutes, Axiom every 15 |
| `ACTIVE` | interactive grace window | stays awake for 8 seconds after last button activity |
| `ECO` | no host and no recent activity | retained display; deep sleep; 30-second aligned probe wake |

USB Host loss must be continuous for 1 second before `HOST → ACTIVE`; idle then
causes `ACTIVE → ECO` after 8 seconds. Reconnect during either grace period
returns to `HOST`. Runtime transitions preserve the current View and page.

**Important hardware limitation:** `HOST` does not mean external power or
charging. Arduino `HWCDC::isPlugged()` observes USB SOF traffic. This board
routes neither USB VBUS nor charger STAT to a firmware-readable GPIO, so a
charge-only wall adapter is indistinguishable from battery and remains `ECO`.
Detecting arbitrary cable power would require a hardware change. USB insertion
cannot wake deep sleep either; the next 30-second probe detects it, so entry to
`HOST` may take up to 30 seconds.

Screen/network cadence:

- Dashboard: HH:MM, sensors, and battery update at minute boundaries.
- Runtime-badge redraws reuse the retained current-minute Dashboard sample;
  they do not silently resample sensors or battery.
- Deck: no date/clock chrome; redraw only for navigation, current-page data,
  battery/STALE state, polarity, or runtime badge changes.
- Silent probe wakes and unchanged fetches do not push a frame.
- Producer normally publishes every 15 minutes. Axiom `HOST` fetches every 15
  minutes; configured Allowance `HOST` fetches every 300 seconds. Token rides in
  the same worker/Wi-Fi session every 15 minutes in `HOST`, and no more often
  than hourly in battery modes. BOOT always fetches both. A failed Allowance
  HOST fetch retries after 60 seconds. A Token-only failure waits for the next
  Allowance run, with the footer showing that actual retry time. `ACTIVE`/`ECO`
  keep adaptive 30 min → 1 h → 2 h
  intervals; low battery may extend that to 4 h.
- Wi-Fi/NTP/Axiom work runs as one background refresh. The main loop continues
  polling USB SOF, buttons, and the console; state/NVS/RTC/display reconciliation
  stays serialized on the main task after the worker finishes.
- Page navigation never starts Wi-Fi. Console `f` is the explicit manual fetch.

## Build + Flash

Create `include/config.h` from `include/config.h.example` and fill Wi-Fi/Axiom +
`LOCAL_TZ`. USB port suffixes drift and another ESP32-S3 may be attached: map
the call-out device to the board-in-hand's USB serial/MAC before every flash.
Never reuse a port copied from an older session. Commands below assume:

```bash
RCLCD_PORT=/dev/cu.usbmodemNNNNN  # replace only after verifying board identity
```

```bash
pio run
pio run -e waveshare-s3-rlcd-dev
pio run -e waveshare-s3-rlcd-dev -t upload --upload-port "$RCLCD_PORT"
```

Explicit production build and flash:

```bash
pio run -e waveshare-s3-rlcd
pio run -e waveshare-s3-rlcd -t upload --upload-port "$RCLCD_PORT"
```

Native logic tests (palette, parser, layout engine, ST7305 1 bpp packing):

```bash
pio test -e native
```

From the repo root, `just native-rlcd` and `just build-rlcd` (via `scripts/pio-run.sh`) set `PLATFORMIO_CORE_DIR` and auto-select a compatible macOS Xcode `DEVELOPER_DIR` when the active host linker predates the installed SDK.

Monitor without DTR/RTS toggling the S3 into download mode:

```bash
uv run tools/serial_probe.py --port "$RCLCD_PORT" --seconds 20
```

The probe uses a raw call-out-device open because pyserial resets this S3 even
when configured with DTR/RTS false. In production, USB disappearance and the
probe's clean disconnect message are expected at deep-sleep entry.

## Buttons

Only BOOT and KEY generate firmware events. PWR is a hardware power key: short
press powers on, long press powers off, and normal taps do not reach the app.

| Gesture | Dashboard | Deck |
|---|---|---|
| **KEY** (GPIO18) tap | Page 1 | next page; cycles Deck ring |
| **KEY** (GPIO18) hold ≥ 600 ms | pin/unpin `HOST` carousel | on Monument Page: cycle **Heatmap Window** (`1Y → 6M → 3M → 1Y`, NVS persisted, static redraw); on other pages: pin/unpin 90s `HOST` auto-carousel (`HOST` ↔ `PINNED`, NVS persisted) |
| **KEY + BOOT** chord | pin/unpin `HOST` carousel | pin/unpin 90s `HOST` auto-carousel (`HOST` ↔ `PINNED`, NVS persisted) on *any* page without stray tap/sync on release |
| **BOOT** (GPIO0) tap, while awake | last page | previous page; Page 1 → Dashboard |
| **BOOT** tap, usage build | Usage + sync now | sync now (Allowance + Token + Monument) |
| **BOOT** (GPIO0) hold ≥ 600 ms | toggle dark mode | toggle & persist 1-bit dark/light polarity |

In the usage build the climate Dashboard is hidden by default (`RCRLCD_SHOW_CLIMATE_DASHBOARD=0`), so KEY cycles through the 3-page Deck ring (`Allowance ⇄ Token ⇄ Monument`; define `RCRLCD_SHOW_CLIMATE_DASHBOARD=1` to include the standalone climate Dashboard). Notice room temperature & humidity from the onboard SHTC3 also appear cleanly as ambient chrome in the Allowance header and the Monument page.
In USB `HOST` mode, unless pinned via `KEY + BOOT` chord (on any page), a 600 ms `KEY` hold on Allowance/Token, or console `l`, the Deck auto-rotates every 90 seconds. Cycling the Monument **Heatmap Window** also resets the 90-second timer. Battery `ACTIVE` and `ECO` modes never auto-rotate.
BOOT tap means "sync now": on any Deck page it stays on that page and fetches
Allowance, Token, and Monument annual history immediately; if climate Dashboard is enabled and active, BOOT opens Allowance and syncs. The
visible page's footer reads `SYNCING · fetching now` until the fetch completes
(a fetch already in flight is joined where possible, not doubled); a successful
Token or Monument sync replays its 0.9-second sweep in HOST.

Production deep sleep arms only GPIO18/KEY as an EXT1 wake source. Each KEY wake
classifies the press by duration: if KEY is already released when setup polls
GPIO18, it advances immediately as a tap with zero wait; if KEY is still held
low, releasing before ~600 ms advances as a tap, while holding across the
threshold runs `KEY` long-press in place on the retained page (cycling the
Monument **Heatmap Window** or toggling `PINNED`) and keeps the 8-second `ACTIVE`
grace window alive. GPIO0/BOOT cannot wake `ECO`: it is polling-only because it
doubles as the download strap. Firmware never sleeps or resets while either raw
button is held. Timer and KEY wakes resume the retained image without a panel
reset or intermediate blank frame.

## Console

- `f` network refresh (Wi-Fi → NTP/RTC + Axiom fetch; the usage build runs BOOT's "sync now")
- `r` re-render current View
- `m` toggle Dashboard ↔ Deck View
- `n` next through Dashboard/Deck ring
- `v` previous through Dashboard/Deck ring
- `l` pin/unpin `HOST` 90s auto-carousel (`HOST` ↔ `PINNED`)
- `w` cycle Monument **Heatmap Window** (`1Y → 6M → 3M → 1Y`), persist to NVS, and redraw immediately
- `t` re-read sensors + redraw current view
- `i` invert light/dark polarity + redraw current View
- `a` toggle the usage view's HOST animation (off gives the static frame for `p`)
- `s` state dump
- `q` dump current `layout_json` (the usage page's encoded view model)
- `p` dump current packed ST7305 framebuffer as hex for screenshot capture
- `c` clear screen
- `R` reboot

For hands-free console testing over USB (no DTR/RTS reset), see
`tools/serial_probe.py`, e.g. `uv run tools/serial_probe.py --send "t,m,s" --seconds 20`.

## Exact screenshots

Use `tools/capture_frames.py` for screenshot previews that match the real
device. It drives the firmware console, captures the current ST7305 1 bpp
framebuffer with `p`, decodes the panel-native byte layout, and writes PNGs:

```bash
uv run tools/capture_frames.py --port "$RCLCD_PORT" --open
```

Output:

- `native/*.png` — exact 400×300 panel frames
- `preview-3x/*.png` — 1200×900 nearest-neighbor previews for Mac inspection
- `contact-sheet.png` — dashboard + all cached deck pages

By default the script sends `f` first to refresh Axiom data. To capture the
current cached deck without network refresh:

```bash
uv run tools/capture_frames.py --port "$RCLCD_PORT" --no-refresh --open
```

## Layout oracle

`tools/preview_dashboard.py` is a headless 400×300 PIL mock of the dashboard
(same black-on-white threshold as the firmware) for iterating layout without
flashing. It is not a deck/page-deck fidelity test; use `capture_frames.py`
when font choice, CJK rendering, sidebar layout, polarity, or page scale must
match the device exactly.

```bash
uv run tools/preview_dashboard.py && open /tmp/rlcd_dashboard.png
```

## Optional weekly allowance source

Define both settings in the gitignored `include/config.h` to replace this
RLCD's Axiom Deck with the private weekly allowance page:

```cpp
#define RCRLCD_USAGE_URL "https://justinyan.app/api/usage-limits"
#define RCRLCD_USAGE_READ_KEY "your-separate-read-key"
```

The device receives **only** the read key, never the existing upload key.
Without these definitions, Axiom behavior is unchanged. Defining only one is a
compile error. In the usage build the climate Dashboard is hidden by default
(`RCRLCD_SHOW_CLIMATE_DASHBOARD=0`), so cold restarts and wakes stay in the Deck
and KEY cycles across the 3-page Deck (`Allowance ⇄ Token ⇄ Monument`; define
`RCRLCD_SHOW_CLIMATE_DASHBOARD=1` to include climate Dashboard in startup/navigation again)
while BOOT syncs now (see Buttons). Usage `HOST` polls every 300 seconds after a success and
retries 60 seconds after a failure; unconfigured Axiom `HOST` stays at 900. After
a failure the retry gate alone schedules the next attempt, so the footer's
`retry HH:MM` is when it really happens, even soon after a success.
Runtime transitions retain the existing host-loss debounce, active grace window
and refresh-gate adjustment. Battery adaptive/low-battery intervals are unchanged.
Console `f` remains the manual network refresh; button navigation never fetches.

The background refresh fetches/validates the complete response; the main task
applies it and builds one PageRecord. The `rcink.usage-limits.v2` `providers`
array defines the active accounts (0–4 rows, unique ids — currently Codex,
Kimi and Gemini/Antigravity); `UsageView` displays known providers in fixed order
`claude`, `codex`, `gemini`, `kimi` (currently Codex, Gemini/Antigravity and
Kimi), with any unknown ids following in payload order. An account removed
upstream drops out of the frame on the next successful fetch; a new one appears
at once, even before its first observation. The collector selects an explicit
stable account per
provider: Codex/Kimi `weekly`, Antigravity `geminiWeekly`. Missing or invalid
sources keep their row as Unavailable; the API retains each account's last
successful observation under its original expiry. Unobserved rows show `--`,
without allowance bars. `claude`/`codex`/`kimi`/`gemini` keep their baked
logos and display names; any other id gets a capitalized name and a generic
initial-in-a-disc logo. With fewer than four rows the row block centers
itself vertically. Used is rounded once and remaining is its integer
complement. Bars represent remaining.

#### Token page

The Deck's second page is a hand-drawn 400×300 Token view
(`src/render/token_render.cpp`) backed by the pure codec and view model in
`src/render/token.cpp` and `src/render/token_view.cpp`. It fetches the private
`GET /api/usage-tokens` at the same origin as `RCRLCD_USAGE_URL`, using the same
read key and bounded HTTPS policy as Allowance. Its schema is
`rcink.token-usage.v2`, mirroring the active token-capable providers from the
Allowance list (0–3 of `claude`/`codex`/`gemini`, vertically centered when fewer
than three; `gemini` sums Antigravity CLI `transcript.jsonl` step tokens in
`Asia/Shanghai`). All daily and lifetime values decode as 64-bit integers.

- Active token providers keep the Allowance names/logos. Each row shows its
  last-seven-day total, today versus the mean of the seven complete days before
  it, a per-row-scaled 30-day bar chart, and lifetime `ODO`. Today's bar is
  hollow and the Usual-day marker is dotted. All-zero rows remain explicit.
- The headline reports a material Provider on a tear, otherwise the busiest
  Provider this week, or waits when every seven-day total is zero. OLD DATA or a
  Token failure suppresses the tear headline without hiding retained numbers.
- The footer independently reports `Synced HH:MM`, `OLD DATA · M4 HH:MM`,
  `SYNC FAILED · retry HH:MM`, `NO CLOCK · waiting for NTP`, `NO WIFI`,
  `NO NTP`, or `SYNCING`. Device-level link failures (`NO WIFI` with
  `No Wi-Fi: <SSID>` headline, or `NO NTP` with `NTP timed out. Clock not set.`
  headline when clock-blocking NTP fails) are shared by both Pages and fall back
  to static coarse intervals (`retry ~1m`, `retry ~30m`, etc.) when no clock is
  set; Page-level `SYNC FAILED` remains independent between Allowance and Token.
- At local midnight the pure view shifts the cached series and appends a zero
  today; it never relabels yesterday. The last complete validated response has
  its own bounded, versioned `token` NVS blob, restored on boot/wake and written
  only when its encoded contents change.
- HOST alone animates a 0.9-second left-to-right bar sweep and odometer roll on
  page entry or Token sync. There is no idle Token animation; ACTIVE/ECO and
  retained deep-sleep frames are static.
- Console `q` on this page prints its `rcink.token-view.v2` `layout_json`, which
  is also the redraw fingerprint and contains the shifted series and bar model.

#### Road view

The usage page is hand-drawn full-screen (`src/render/usage_render.cpp`), not a
DSL page beside the sidebar. `src/render/usage_view.cpp` derives everything it
shows as a pure, natively tested model; the page's `layout_json` carries that
model's JSON, which is both what gets drawn and the redraw fingerprint.

```
* Best bet today: [K] Kimi                         <- headline
[logo]  34%  Claude                     ~6% to spare
             ..........[car]=|========  [pump] (o) 2d 1h
...
Synced 11:34                        [batt] 99% HOST
```

- Each provider is a car on a 150 px road of fuel that ends at a pump. The car
  sits at the share of the weekly allowance already **used**, so the dots behind
  it are what is gone and the solid road ahead is what is left — the same
  amount as the big number. A car parked at the pump is out.
- The tick under the road is **even pace**: the elapsed share of that
  provider's own 604800-second window. A car behind its tick has room to spare;
  a car past it is burning faster than the week allows. The clock and
  countdown at the right are the time until the allowance refills.
- The verdict projects this week's *average* burn rate to the reset:
  `dry 14h early` (bold; days from 48 h) or the leftover, e.g. `~92% to spare`.
  `out of gas` means 0 %. Fewer than ~3.4 hours into a week shows `new week`.
- The headline picks the fresh provider with the most room above its even-pace
  share (remaining % minus time-left %, remaining > 10 %). When a fresh provider
  is at 0 % or ≤ 10 % it says `Codex is napping. Try ...` / `... is almost out`.
- Remaining ≤ 10 % is drawn inverted. Countdowns are hour-granular (`2d 18h`,
  `17h`, minutes only in the last hour) and the pace tick moves one pixel about
  every 67 minutes, so the frame changes about hourly, not every minute.
- Stale data loses its personality: no projection, a hatched road and a parked
  outline car. A single stale row gets its own `3h old` chip. When every known
  row is stale the screen says it once: `No new numbers since HH:MM.` with an
  `OLD DATA` footer (`checked HH:MM`) when the GET worked but the source has
  gone quiet, or `Can't sync. Numbers from HH:MM.` with `SYNC FAILED` when it
  did not. Device-level link failures beat the initial `NO CLOCK · waiting for NTP`
  fallback: Wi-Fi association failure shows `No Wi-Fi: <SSID>` in the headline
  with a `NO WIFI` footer, while clock-blocking NTP timeout shows
  `NTP timed out. Clock not set.` with a `NO NTP` footer (showing `retry ~1m` /
  `retry ~30m` when the clock is unset). An untrusted clock still keeps rows
  stale with `What day is it? Clock not set.` before the first link failure and
  draws no pace ticks. Never-observed rows show `--` and `no data yet`, with no car.
- Source samples expire 10 minutes after they are taken, while the source, its
  uploader and the device each run independent schedules. In `HOST` the device
  pre-fetches 60 s before a fresh sample expires (clamped to at least 60 s after
  the previous fetch), then chases every 60 s while the source is late until
  30 minutes after that sample; then it falls back to the normal 5-minute poll.
  Battery modes keep their adaptive schedule.
- The `gemini` row is Antigravity's Gemini weekly pool, marked by the small
  arch logo.
- In `HOST` only, the view animates at ~7 fps (lane marks scroll, cars bob and
  puff, `Zz` floats), and for 0.9 s after a successful sync or when entering
  the view each car drives in from the start while its number burns down from
  100. ECO/ACTIVE draw the static frame,
  which is also what deep sleep retains. A frame takes ~36 ms on device; the
  loop delivers one every ~135 ms, so stepped motion advances per drawn frame
  (not per millisecond) to avoid aliasing stalls. Stale and napping cars stay
  parked on purpose.

Logos are 1-bit bitmaps baked from the single-colour SVGs in
`tools/usage_logos/` (lobe-icons; marks belong to their owners):

```bash
uv run tools/bake_usage_logos.py          # regenerate src/render/usage_logos.h
uv run tools/bake_usage_logos.py --check  # verify the header is current
```

HTTPS uses GTS Root R4 (certificate source and fingerprint in
`src/net/usage_ca.h`), hostname validation, the existing RTC/NTP clock gate,
`rcink-rlcd/1.0` User-Agent and JSON Accept header. The TCP connect deadline is
10 seconds; TLS handshake, response headers and the body get 20 seconds each.
On the device's Wi-Fi (measured 2026-11-12: ~250 ms RTT with loss to
Cloudflare) a handshake takes 5-10 s, about 1 s of it certificate checks, so
10-second deadlines failed roughly one fetch in six. Each fetch logs separate
DNS, GET, and body timings (`GET status=N dns=M ms get=K ms (attempt A)`).
Within the same Wi-Fi session, transient transport or gateway errors
(`status < 0`, `502`/`503`/`504`, DNS failure, or an incomplete body) retry once
after 500 ms with a fresh TLS/HTTP client; 4xx responses, header rejections,
oversized bodies, and schema decode failures never retry. HTTP/1.0 requests
close-delimited identity bodies; unexpected transfer encoding is rejected.
Bodies are bounded to 4096 bytes for Allowance and 8192 bytes for Token.
Redirects are refused, so the read key cannot follow a Location header. A failed
HTTP/status/schema/size/time check preserves the last good snapshot and
displays `SYNC FAILED`, while `RTC_DATA_ATTR` diagnostics retain the sanitized
last fetch and last failure details for console `s` and diagnostics page `d`;
serial logs expose only sanitized diagnostics/status codes. An unknown clock
cannot display fresh rows or cause a TLS bypass. No insecure TLS fallback
exists.

### Persistent/offline lifecycle

The main task merges only completely validated responses. A strictly newer
per-provider `fetchedAt` replaces that slot, including lower usage after a reset.
Equal/older samples cannot change percentages or timestamps. Equal-time `stale`
evidence is retained even if the RTC is slightly behind the Worker. A previously
known provider returned as `unavailable` retains its values marked stale; a
never-seen provider remains unavailable. Only a newer valid sample clears this
stale evidence. No login/revocation explanation is inferred.

A single bounded, versioned response blob in the existing `rcpaper` namespace on
`rcdata` stores merged values and original sampling/expiry/reset times. It uses
the same decoder on restore; malformed/unsupported/oversized saved data cannot
partially replace state. Percentages retain double round-trip precision so a
restart cannot change rounding near a half percent. The legacy Axiom pages blob
is untouched. Snapshot writes happen after validated changes; duplicates skip
writes and a failed write retries on a later successful poll. Storage failures
are reported without payloads; until a commit succeeds the newest values exist
only in RAM. NVS, RTC and display work remain on the main task.

During ordinary loop/probe wakes, the page re-evaluates source expiry, source
reset and sampling age without HTTP. Unknown time shows the clock warning and
stale/unknown age, and the existing RTC/NTP gate still prevents HTTPS without
usable time; no TLS bypass exists. Only changed visible content is redrawn.
The retained frame fingerprint avoids pushing an identical frame at a silent
probe wake. RTC retains fetch/backoff metadata and failure display state across
deep sleep, preserving the adaptive battery/low-battery policy. No new wake
source, network button action or retry loop is added. Cold restart immediately
shows restored values in the saved view before the next network completion.

Five-minute source/upload/device schedules run independently and do not promise
five-minute end-to-end latency. Firmware rollback restores the prior binary while
leaving the additive `usage`/`token`/`monument` NVS blobs and Just Ship D1 tables
intact.
