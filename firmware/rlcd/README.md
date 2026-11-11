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

Both builds currently boot in dark polarity (`RCRLCD_DEFAULT_DARK_MODE=1`):
the final packed 1 bpp frame is inverted after normal black-on-white rendering.
Use console `i` to toggle light/dark live and redraw the current screen.
The default production environment retains the last ST7305 frame during deep
sleep. `waveshare-s3-rlcd-dev` uses the same state machine but never sleeps, for
recovery and development.

## Runtime modes

Dashboard, Deck, and error screens carry one truthful mode badge:

| Badge | Meaning | Behavior |
|---|---|---|
| `HOST` | USB **data host** detected from 1 ms SOF frames | stays awake; USB console + both buttons available; fixed 15-minute fetch |
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
- Producer normally publishes every 15 minutes. `HOST` fetches every 15
  minutes, including after a failed attempt. `ACTIVE`/`ECO` keep adaptive 30
  min → 1 h → 2 h intervals; low battery may extend that to 4 h.
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
| **KEY** (GPIO18) tap | Page 1 | next page; last page → Dashboard |
| **BOOT** (GPIO0) tap, while awake | last page | previous page; Page 1 → Dashboard |

Production deep sleep arms only GPIO18/KEY as an EXT1 wake source. Each KEY wake
advances exactly once through Dashboard ⇄ Page 1 ⇄ … ⇄ Page N; its eventual
release is consumed rather than counted as a second press. GPIO0/BOOT cannot
wake `ECO`: it is polling-only because it doubles as the download strap. There
are no long-press or double-click actions. Firmware never sleeps or resets while
either raw button is held. Timer and KEY wakes resume the retained image without
a panel reset or intermediate blank frame.

## Console

- `f` network refresh (Wi-Fi → NTP/RTC + Axiom fetch)
- `r` re-render current View
- `m` toggle Dashboard ↔ Deck View
- `n` next through Dashboard/Deck ring
- `v` previous through Dashboard/Deck ring
- `t` re-read sensors + redraw dashboard
- `i` invert light/dark polarity + redraw current View
- `s` state dump
- `q` dump current `layout_json`
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

## Optional weekly allowance source (#2–#3)

Define both settings in the gitignored `include/config.h` to replace this
RLCD's Axiom Deck with the private weekly allowance page:

```cpp
#define RCRLCD_USAGE_URL "https://justinyan.app/api/usage-limits"
#define RCRLCD_USAGE_READ_KEY "your-separate-read-key"
```

The device receives **only** the read key, never the existing upload key.
Without these definitions, Axiom behavior is unchanged. Defining only one is a
compile error. Climate Dashboard, existing Deck/button navigation and runtime
power policy are preserved. #2 keeps the existing startup view and polling
cadence; #4 owns the selected usage startup view and five-minute HOST cadence.
Console `f` remains the manual network refresh; button navigation never fetches.

The background refresh fetches/validates the complete response; the main task
applies it and builds one ordinary DSL/PageRecord. The fixed slots are Claude,
Codex, Kimi and Gemini (Antigravity). The #3 collector selects an explicit stable
account per slot: Claude/Codex/Kimi `weekly`, Antigravity `geminiWeekly`. Missing
or invalid sources are omitted; the API retains each slot's last successful
observation under its original expiry. Unobserved slots show `--` and Unavailable,
without allowance bars. Used is rounded once and remaining is its integer
complement. Bars represent remaining.

Each row has a provider/remaining heading, a compact used/status/source-age/reset
line and a remaining bar. Age is truncated to whole minutes/hours/days/years
(`m`/`h`/`d`/`y`, `<1m` for under a minute); `R` introduces the source reset in UTC
(`Z`), or `R--` if none was supplied. `age?` and the clock warning mean local time
cannot establish source age. Status is `OK` or `STALE` per provider. Successful
HTTP polling never freshens source timestamps. All four rows fit beside the
existing 56px sidebar without changing the font ladder or monochrome palette.

HTTPS uses GTS Root R4 (certificate source and fingerprint in
`src/net/usage_ca.h`), hostname validation, the existing RTC/NTP clock gate,
`rcink-rlcd/1.0` User-Agent and JSON Accept header. Connect/TLS/read deadlines are
10 seconds each. HTTP/1.0 requests close-delimited identity bodies; unexpected
transfer encoding is rejected. The body is bounded to 4096 bytes with a total
10-second body deadline. Redirects are refused, so the read key cannot follow a
Location header. A failed HTTP/status/schema/size/time check preserves the last
good **in-memory** snapshot and displays `fetch failed`; serial logs expose only
sanitized diagnostics/status codes. An unknown clock cannot display fresh rows
or cause a TLS bypass. No insecure TLS fallback exists.

### Deliberate lifecycle boundary

#2 keeps source timestamps in RAM and does not persist a usage snapshot into
the legacy Axiom cache. On reset/deep-sleep wake, values start unavailable until
a successful refresh. During this slice, freshness is recalculated when building
a page after a refresh; automatic offline expiry/redraw between refreshes and
validated restore after restart belong to #5. The unchanged 15-minute HOST
cadence can exceed OpenUsage's source expiry; do not treat this slice as the
finished unattended dashboard. Existing Axiom cache data remains intact.

### #3 local handoff

The native suite passes **81/81**, including two new four-provider tests using
the same anonymous observations as Just Ship's source → D1 → GET integration
fixture. They check complementary rounding (including half percentages), valid
0% used, source ages, independent reset/expiry state, remaining bars and geometry
inside the 344×300 content area at nominal font sizes. Four full rows plus the
unknown-clock warning and fetch-failure header fit without a scaling pass.
Unavailable rows remain bar-free. These geometry checks use conservative text
measurements; they do not establish real glyph/framebuffer or physical readability.

Both production `waveshare-s3-rlcd` builds passed with safe placeholder config:
unconfigured **62,960 bytes RAM / 1,394,005 bytes flash**, usage-enabled
**62,956 bytes RAM / 1,395,929 bytes flash**. Local private config was preserved
and restored by rename, without reading its contents. Tool/dependency/build
state for the completed builds stays inside this worktree. Those placeholder
artifacts were not committed and were not rollout firmware. Two preliminary PIO
commands failed before building because they ran at the repo root; corrected commands ran from `firmware/rlcd`.

Only the usage page builder/tests and these notes changed. Scheduling, HOST
polling, startup selection, persistent cache and automatic offline redraw remain
with #4/#5. #4 is implemented/reviewed separately and is not incorporated here.

### #3 supervisor rollout checkpoint (2026-11-11)

The supervisor reports that GPT-6 Astra pre-rollout review passed, followed by
collector **11/11**, Worker usage **8/8** and native usage **6/6** checks. The
reviewed M4 collector uploaded all four selected snapshots; exact API fields
agree with the source observations. No private account IDs or live values are
recorded here.

The supervisor built production firmware and flashed the verified board with a
successful written-data hash check. An external Wi-Fi outage initially produced
`NO_AP_FOUND`; the user resolved connectivity and the actual device now reports
**GET 200**. This did not require an implementation change.

Final four-row framebuffer capture/comparison is **pending**. Do not mark #3
acceptance complete or publish its PRs until the supervisor confirms source/API/
framebuffer percentages, labels, remaining bars and unclipped 400×300 layout.
Physical readability and button/climate acceptance remain supervisor-owned.
Private live configuration and binaries may now be present in this worktree;
this documentation update neither reads them nor rebuilds firmware. No #4
scheduling/startup changes or #5 persistence/offline changes are included.

### Local evidence and pending acceptance

On 2026-11-10 the following passed in the isolated #2 worktree:

- Native suite: **79/79** tests, including four new response/view tests. The
  anonymous Claude response uses the same sample/expiry/reset and percentages
  as Just Ship's source-fixture → projection → PUT → test D1 → GET test. It also
  covers half-rounding, missing slots, stale/unknown time, malformed/oversized
  responses and last-good visible values on failure. Layout geometry is tested
  inside the existing 344×300 content area beside the 56px sidebar; this is not
  a real framebuffer/font or physical-device acceptance claim.
- Production `waveshare-s3-rlcd` build with unconfigured safe placeholders:
  success, 62,960 bytes RAM and 1,392,997 bytes flash.
- The same production build with usage-enabled safe placeholders: success,
  62,956 bytes RAM and 1,394,465 bytes flash. Original private configuration was
  restored byte-for-byte afterward. These build artifacts contain placeholders
  and are not deployment firmware.

Commands (run from `firmware/rlcd`):

```sh
DEVELOPER_DIR=/Applications/Xcode-27.2.0.app/Contents/Developer uv run --with platformio==6.1.19 --with esptool==4.9.0 pio test -e native
DEVELOPER_DIR=/Applications/Xcode-27.2.0.app/Contents/Developer uv run --with platformio==6.1.19 --with esptool==4.9.0 pio run -e waveshare-s3-rlcd
```

As of 2026-11-11, supervisor evidence (`m4-deployment.json`,
`m4-live-api-results.json`, `supervisor-state.json` and
`i2-acceptance-evidence.json`) confirms the backend deployed from M4 staging
based on Just Ship `02ead98`: migration 0004, separate read authentication,
real cached Claude upload/read-back, duplicate suppression and legacy API/D1
preservation passed. HTTP/1.0 GET returned bounded JSON with Content-Length and
no transfer encoding. M4 SSH and M4-native Cloudflare authorization succeeded.

GPT-6 Astra reviewed the feature twice. The bar-height finding was fixed in
rcink `d9d3ebf`; final review of `d9d3ebf` / Just Ship `02ead98` had no actionable
findings. The staging rollout preserved a pre-existing, already-deployed
WordPress retry patch absent from the reviewed Just Ship branch. Future Worker
deployments/rollbacks must preserve that patch; its checksum and boundary are
recorded in Just Ship's `docs/usage-limits.md`. No unrelated WordPress code was
incorporated, and the original dirty M4 checkout remained untouched.

Supervisor hardware preflight (`i2-hardware-preflight.json`) verified the
ESP32-S3 identity, 16 MiB flash / 8 MiB PSRAM, and the original full 16 MiB
backup's SHA256. With user-provided Wi-Fi, the supervisor then built production
firmware and flashed that verified board with written-data hash verification.

Device acceptance on 2026-11-11 (`i2-device-acceptance.json`,
`i2-device-api.json`, `i2-device-usage.png`) passed actual ESP32 authenticated
GET 200 with CA/hostname verification and NTP. During that run, source/API/layout
agreed on Claude 63% used / 37% remaining and sampling/reset UTC; the other three
slots were unavailable. The actual 15,000-byte framebuffer decoded to a readable
400×300 image with the remaining bar visible and no clipping. These percentages
are recorded acceptance evidence, not fixed or necessarily current values.

Ticket #2 acceptance is **complete**: the user confirmed that the physical
display and KEY/BOOT navigation operate normally. This completes the user
confirmation following the source/API/framebuffer and production-device checks.
Supervisor retains device, private configuration, M4 and live-service access.
No periodic uploader is enabled; #3–#5 retain their provider, cadence/startup
and persistent offline scope.

For supervisor-owned rollback, stop allowance uploads and restore the
appropriate prior Worker/firmware while preserving unrelated production fixes.
The additive D1 table and original daily token uploader/heatmap can remain.
The backend/operator contract is in Just Ship's `docs/usage-limits.md` and ADR
0005. Merge and any further deployment remain supervisor-owned.
