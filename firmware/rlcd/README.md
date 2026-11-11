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
  minutes; configured usage `HOST` fetches every 300 seconds, including after a
  failed attempt. `ACTIVE`/`ECO` keep adaptive 30
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
power policy are preserved. Usage is the default only when no NVS view choice exists. Cold restart and
Timer/KEY deep-sleep wakes restore the saved view, including climate; KEY then
advances once. An existing saved climate choice is respected when enabling usage. KEY/BOOT still navigate Usage ↔ climate; no new
view or network-dependent button action is introduced. Usage `HOST` polls every
300 seconds after success or failure; unconfigured Axiom `HOST` stays at 900.
Runtime transitions retain the existing host-loss debounce, active grace window
and refresh-gate adjustment. Battery adaptive/low-battery intervals are unchanged.
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
good snapshot and displays `fetch failed`; serial logs expose only
sanitized diagnostics/status codes. An unknown clock cannot display fresh rows
or cause a TLS bypass. No insecure TLS fallback exists.

### Persistent/offline lifecycle (#5)

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

Local #5 verification (2026-11-11): full native suite **93/93**, including actual
HTTP consumer failure fixtures (timeout, connectivity, 401/429, stalled/truncated,
oversized, unsupported/malformed response and unknown clock), production NVS
serialization with a Preferences IO fixture, restart/view restoration, per-slot
merge, offline expiry/reset, same-second content changes and precision. Existing
navigation/runtime/HOST/ECO/low-battery/retained-display policy suites pass.
These are automated boundaries, not ESP32 TLS handshakes or physical outage tests.
Production builds with unconfigured and usage-enabled safe placeholders passed;
only local `.pio/core` and `.pio/uv-cache` were used. The prior config file was
preserved/restored by rename; no private contents or binaries were inspected.

Deployment/rollback belongs to the supervisor after published-PR review.
This firmware uses the already-deployed four-slot API; #5 needs no Worker,
migration, credential or uploader change. Build the reviewed firmware with the
supervisor-owned configuration and normal verified identity/backup procedure.
Rollback restores the prior firmware; the separate usage NVS blob and additive
D1 table may remain. If backend rollback is needed, preserve the unrelated
production WordPress retry patch. Stop only the allowance LaunchAgent if needed;
leave the original daily collector, completion state, heatmap and data intact.
Five-minute source/upload/device schedules remain independent and do not promise
five-minute end-to-end latency.

The user waived #5 physical outage/disconnection/restart and HOST/ECO acceptance.
Those checks are **waived, not passed**. No M4, live API, device, flash, secret
configuration or simulator access was used here. Supervisor focused verification
also passed native usage/HTTP/persistence/navigation **23/23** and Worker usage/D1
**9/9**, with clean diff checks. Publication is authorized; root owns fresh
GPT-6 Astra review of the published PRs, merge and rollout.

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

### #3 completed hardware acceptance (2026-11-11)

The supervisor reports that GPT-6 Astra pre-rollout review passed, followed by
collector **11/11**, Worker usage **8/8** and native usage **6/6** checks. The
reviewed M4 collector uploaded all four selected snapshots; exact API fields
agree with the source observations. No private account IDs or live values are
recorded here.

The supervisor built production firmware and flashed the verified board with a
successful written-data hash check. An external Wi-Fi outage initially produced
`NO_AP_FOUND`; the user resolved connectivity and the actual device now reports
**GET 200**. This did not require an implementation change.

**#3 hardware acceptance is complete.** Supervisor evidence is recorded in
`i3-device-acceptance.json`, `i3-device-api.json` and `i3-device-usage.png` for
reviewed runtime commits rcink `72ee727` / Just Ship `8750ef0`. Actual source/API/
layout percentages, status, original reset times, source age and complementary
remaining bars agree for all four providers. The captured remaining values were
Claude **37%**, Codex **33%**, Kimi **94%** and Gemini **100%**; these are historical
acceptance observations. The exact 400×300 framebuffer is readable without
clipping, including `Gemini (Antigravity) 100% left` and the existing sidebar.
The D1 latest-snapshot table contains exactly **four rows**.

Actual console next reached the climate Dashboard with `env_ok=1`; previous
returned to Usage. Physical controls were user-confirmed during #2 and their
input paths are unchanged. No further user confirmation is required for #3.

Fresh focused checks before publication passed native usage **6/6**, collector/
projection **29/29** and Worker usage/D1 **8/8**. Both branches were fetched and
rebased onto `origin/main` without changes (rcink base `d9c479c`, Just Ship base
`16facba`). The reviewed implementation is unchanged; only documentation was
committed during closeout. No production firmware rebuild or private config/
binary access occurred in this closeout.

The GitHub workflow requires self-hosted runners, but `justinyanme/rcink` has
**zero registered runners** and existing CI runs are queued. Remote CI is not
claimed green; local checks and supervisor hardware evidence are recorded above.
Merge the companion Just Ship PR **before** this firmware PR. Published-PR Astra
closeout and merging remain supervisor-owned; no further deployment/device/M4
operation is part of this closeout. #4 remains separate and #5 remains outside
this branch.

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

### #4 automatic updates: local verification and handoff

Before the #3 integration rebase on 2026-11-11, in the assigned isolated worktree,
native tests passed **83/83**,
including usage HOST success/failure boundaries, unchanged battery/low-battery
and Axiom intervals, cold startup selection and deep-sleep/navigation behavior.
The existing runtime, interaction, retained-display and wake tests also passed.
Production `waveshare-s3-rlcd` builds passed with both unconfigured safe
placeholders and usage-enabled safe placeholders. The original local config
was restored by rename without reading or printing its private contents. No
generated firmware is committed or suitable for rollout.

The commands above used additional worktree-local caches:
`PLATFORMIO_CORE_DIR="$PWD/.pio/core" UV_CACHE_DIR="$PWD/.pio/uv-cache"`.
No simulator, serial access, flash, M4 mutation or live service access occurred.

The independent M4 uploader's private-env migration, install/disable commands,
filtered Worker tail and expected D1 metrics are in Just Ship's
`docs/usage-limits-automatic.md`. After independent review, the supervisor must
enable that job and verify at least two automatic cycles through real source,
API, device and framebuffer, including newer samples with unchanged percentages.
Source/upload/device schedules are independent: five-minute intervals do not
guarantee five-minute end-to-end latency. Real startup/buttons, HOST/ECO and
power checks, D1 write telemetry and four-row storage bounds remain live
acceptance criteria for #4. Merged #3 supplies accepted four-provider mapping,
detail layout and completed Wi-Fi/device acceptance; those are not pending.
#5 owns persistent usage values and offline expiry. #4 does not claim those
lifecycle criteria complete.

After the #3 merge, #4 rebased without conflicts onto `origin/main`
`88c91f48e49eb5646f7976d6f7eb558fd71d738b` (PR7); the companion Just Ship base is
`ac89e97d42087bb4662896c9e7abf26971d04f31` (PR45). #4 runtime remains unchanged
from reviewed rcink `87c66a5336b67cb3abc1cab734612921c1c94843`. The imported #3
renderer/tests and completed hardware evidence are preserved. Using the same
local caches and pinned PIO command above, focused `native` suites
`test_native_usage`, `test_native_refresh_schedule`, `test_native_network_policy`
and `test_native_navigation` passed **26/26**. Production builds were not repeated:
#4 runtime is unchanged and #3 completed its own builds/hardware acceptance.
Only #4 docs changed during this integration closeout. No private config, M4,
device or live service access occurred during that integration closeout.

### #4 supervisor live rollout and user-waived remaining acceptance (2026-11-11)

Reviewed production firmware from rcink `23e8bbf` was flashed with hash
verification and starts in Usage HOST. Initial all-four-provider values, status
and reset times agree across source, API and framebuffer. The reviewed M4
LaunchAgent from Just Ship `7641c65` is installed; its initial automatic upload
of four providers succeeded. No new Worker deployment was needed. The existing
daily job and production WordPress retry patch were verified unchanged.

After initially pausing observation, the user explicitly waived the remaining
two-subsequent-automatic-300-second-cycle acceptance and requested merge, then
#5. Those criteria are **waived, not passed**; the initial hardware evidence
above remains valid. Observation/tail stopped while the automatic service
remains running. No additional tests, rebuilds or live acceptance are requested.
Root owns fresh Astra closeout and backend-first merge of the crosslinked PRs.
Remote CI is not passed: there are zero registered self-hosted runners. Private
config and build artifacts remain untouched by the builder.
