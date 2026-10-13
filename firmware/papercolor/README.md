# rcpapercolor

Battery-powered colour e-ink dashboard for the M5Stack PaperColor that renders
a generic JSON layout-tree fetched from Axiom. Producers run on macOS and push
one event per dashboard page; the device wakes every 30 min for scheduled
refresh, renders only changed content, and deep-sleeps on battery. External
power keeps the CPU/USB console, buttons and adaptive refresh scheduler running.
Page changes are button/console-only.

Two halves of one repo:

- `producer/` — Python (uv) on Mac. Pulls Claude/Codex usage, pushes pages.
  Also supports one-off remote image pages via `uv run image-fetch <url>`.
  See `producer/README.md` for setup.
- `src/` + `platformio.ini` — ESP32-S3 firmware. PlatformIO + M5Unified +
  M5GFX. See below.

## Firmware quickstart

1. Copy `include/config.h.example` to `include/config.h`. Fill in WiFi credentials,
   Axiom token, and the Axiom Root CA PEM.
2. Make sure the producer has written at least one event to the
   `rcpapercolor` Axiom dataset (run `cd producer && uv run claude-fetch`).
3. From the repository root, build and test:

   ```bash
   just build-papercolor
   just native-papercolor
   ```

4. This unit is MAC `44:1B:F6:C1:31:90`; the current operator-authorized port is
   `/dev/cu.usbmodem20111401`. No autodiscovery, alternative ports or reconnect
   loops. If absent, explicitly wake the board with a button before starting.

   ```bash
   just flash-papercolor /dev/cu.usbmodem20111401 .cache/papercolor/flash-001
   just observe-papercolor /dev/cu.usbmodem20111401 .cache/papercolor/observe-001
   ```

`flash-papercolor` builds, opens exactly the named port, checks MAC and reads
only the small partition table, then writes **only app0 at 0x10000** and verifies
it. No full-flash backup: source/build artifacts provide the reflash path.
The same connection handles checks and write at 921600 baud; success uses
watchdog reset. A SHA-256 manifest identifies the application. NVS, cached
pages, partition table and bootloader remain intact. Failed writes leave the
loader stopped; retry deliberately with a complete application, never chip erase.

`observe-papercolor` starts P4 video first, waits for a complete timeline record,
then sends `pdrp` on one PaperColor connection: supply status → diagnostics paint
→ return to current page → supply status. Saves raw serial log, command-write
monotonic timestamps, original video frames and camera timeline. Inspect the
actual frames; successful capture is not visual acceptance. Camera and target
commands use distinct devices. Capture uses the local sibling `../rceye` repo.

## Supply policy and console

One firmware replaces the former separate no-sleep dev environment:

- PM1 register `0x04` is a **bitmap**, per [datasheet v1.9](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf):
  bit0=5VIN valid, bit1=5VINOUT valid, bit2=battery valid. Any external bit
  takes priority. Captured `0x05`, VIN=4598 mV means USB + battery present.
  Pinned M5PM1 `37d55c2` incorrectly declares this an enum; do not use it.
  Full battery/charge termination does not imply unplugged.
- External source: CPU remains awake; console, buttons and adaptive scheduled
  fetches continue. Display waveforms are unchanged in this tooling stage.
- Battery: finish any active work, then display sleep → SPI tristate → rail
  shutdown → deep sleep. Preserve remaining refresh deadline when unplugging.
- Supply read failure/unknown source or missing display initialization is fatal;
  never interpret an I2C error as battery power.
- Plugging into a sleeping board may need one button press; USB insertion wake
  is not implemented. External power presence does not imply a USB data host.
- `p`: supply source, VIN mV, battery mV, uptime and next fetch. `s`: cached page
  state. `f`: fetch/render. `d`: diagnostics. `r`: render current page (dedup can
  skip unchanged content). `h`: full command help. Explicit `R` reboots.

```bash
uv run --project producer dev-cli session ps \
  --port /dev/cu.usbmodem20111401 --timeout 10 \
  --save .cache/papercolor/state.log
```

The host opens once with inactive DTR/RTS and exclusive ownership, receives while
sending, and fails on missing/disconnected ports or short writes. It never
reopens/resends. `--start-delay` collects startup logs before the first command;
`--events FILE` records command-write start times on the host monotonic clock.
Output files must be new. A write timestamp is not firmware execution time. Captured fatal boot/power
errors stop the session immediately; logs remain available.

## USB reset and acceptance boundaries

Opening native USB-Serial/JTAG previously caused `USB_UART_CHIP_RESET`, even
with RTS/DTR inactive. Do not infer a natural wake reason after opening a host
console. This occurred again in the 2026-10-13 recording. Inactive control lines do
not guarantee a reset-free open on this unit. ROM-download wedging previously left the
battery draining; a silent enumerated port is not proof of running firmware.
If ROM synchronization fails, keep USB connected and hold the side PWR/RESET
button about **3 seconds** to enter download mode; ordinary single/double clicks
are not equivalent. [Official recovery procedure](https://docs.m5stack.com/en/core/PaperColor#download-mode).
The tool opens once and bounds ROM synchronization to three attempts.

After a flash, require valid `p`/`s` replies, increasing uptime on the same
connection, valid VIN/source, and camera evidence of a real paint. External
power deliberately leaves USB enumerated; the former rule “port present means
wedged” no longer applies. Battery sleep must be verified separately after
physical unplug; USB disappearance alone only proves disconnection. Replug
and inspect the next wake evidence without treating console-induced reset as
natural wake. Full-charge source behavior and charger-only operation also need
physical validation.

The old dev-build note guessed PM1 I2C GPIO12/11. Pinned M5GFX/M5Unified source
uses SDA3/SCL2; GPIO12/11 are panel reset/BUSY. Board fallback plus a fatal
zero-dimension check makes bring-up failures visible; hardware must establish
whether the old autodetection failure still occurs.

### Tooling-stage evidence (2026-10-13)

- MAC `44:1B:F6:C1:31:90` confirmed on the authorized port. User explicitly
  declined full-flash backups; default tool now reads only the partition table.
- Corrected application flashed and hash verified: `flash-009/manifest.json`
  and `flash-009.log` under `.cache/papercolor/2026-10-13/`.
- New firmware boots PaperColor 400×600 with I2C SDA3/SCL2; battery ~3.88 V and
  SHT40 readings valid. Old dev-build zero-display failure did not reproduce.
- Initial PM1 enum decoding caused a fatal/reboot loop (`observe-001`). The
  diagnostic capture `power-diagnostic-008.log` established raw `0x05`, valid
  VIN; the datasheet bitmap fix resolves it. Ordinary PWR clicks did not
  recover ROM communication; the documented 3-second hold did.
- `observe-002`: one 55-second session, two physical paints (diagnostics and
  dashboard), 17.365/17.373 seconds each; uptime increased without reboot.
  P4 video contains macroblocked frames: use JPEGs for settled-image acceptance,
  not video artifacts for fine optical timing or calibrated color claims.
- Battery unplug + A+B wake: `battery-wake-settled.jpg` shows diagnostic WAKE=2,
  increased from this boot's WAKE=1, proving deep sleep followed by button wake.
  USB disappearance alone was not used as the sleep oracle.
- Replug + button: `replug-001.log` shows external source `0x05`, VIN about
  4.60 V, uptime 2012→46025 ms on one connection. `replug-001.jpg` confirms the
  full dashboard restored and readable. Opening the console itself resets USB.
- Build and native suite (45) passed. Producer suite 447 passed,
  1 skipped (TLSR grid); expanded console tests 10 passed. Use `just
  producer-test`: root pytest misses producer's asyncio configuration.
- Still unverified: charger-only, full-charge and a complete 30-minute scheduled
  fetch interval. Fast waveforms untouched in this stage.

### Fast-refresh compatibility findings (2026-10-13)

- Pinned `Panel_ED2208` maps `quality/text/fast/fastest` to dithering only;
  all use the same full-frame transfer and OTP waveform. Mode names do not
  establish faster physical refresh.
- DayoTag's 799-byte RAM LUT and old/new pixel selectors are not demonstrated
  compatible with EL040EF1. Initialization differs (`00 0F A9` versus
  PaperColor `00 5F 69`); shared refresh opcodes do not prove LUT compatibility.
- [FreeInk's same-panel driver](https://github.com/Free-Ink/freeink-sdk/blob/e91e34e3da3bf87b6c3c896fe1b3d6dda1a8c7c2/libs/display/FreeInkDisplay/src/driver/Ed2208M5Driver.cpp)
  interrupts OTP about 340 ms after BUSY assertion, then resets/reinitializes.
  This yields black/yellow mono, not fast full color. Its author reports
  darkening/color fade after prolonged use and requires complete-waveform
  cleanup. Windowed interruption also produced bands; its fast path uses the
  whole panel. These are community observations, not vendor qualification.
- Physical comparison rejected every experimental mode for normal pages.
  Dashboard, diagnostics and button navigation retain native full refresh;
  unchanged-page dedup and button coalescing avoid unnecessary refreshes.
  Faster waveforms remain explicit, bounded console experiments only.

`just trial-papercolor PORT NEW_DIR N` verifies the raw native transport first.
Then choose `F` (color-fast), `Q` (color-quality), `M` (mono), or `C`
(mono-clear), or `I` (340 ms interrupted native mono); `d` renders normal native
diagnostics. At most five explicit
modes per invocation. The host waits for
firmware completion before each P4 JPEG, then restores the dashboard with a
complete native waveform and captures it again. One USB connection, no retry.
Camera failure also requests one native restoration before closing the port.
Logs/events/photos stay in NEW_DIR. BUSY completion measures controller work,
not optical quality; inspect every test and restoration photo.

To compare all four sf32rc modes, keep PaperColor connected to the authorized
USB port, close other serial monitors, and frame the entire display in P4.
The sibling `../rceye` project must have its P4 connection configured. Run from
the rcink root; use a new output directory on every run:

```bash
just trial-papercolor /dev/cu.usbmodem20111401 .cache/papercolor/manual-01 FQMC
```

Sequence: F=color-fast, Q=color-quality, M=mono, C=mono-clear. Each paints a
test chart, saves its P4 photo, restores the normal dashboard with native
refresh, then saves a restoration photo. Allow roughly two minutes for the
four-mode sequence. Inspect `00-F.jpg`, `01-Q.jpg`, `02-M.jpg`, `03-C.jpg`
alongside each `*-restored.jpg`; `serial.log` contains measured transaction
times. Restoration starts immediately after capture, so use saved photos
for comparison. To test just one mode, replace `FQMC` with `F`, `Q`, `M` or `C`
and choose another new output directory. These commands test charts, not
automatic fast rendering of dashboard content. An absent port requires a
button wake; the script never searches for another port.

`src/dev/waveform_trial.cpp` reuses the existing M5GFX SPI bus and board's
native initialization/analog settings, including its post-power-on booster
sequence. Custom modes also copy sf32rc's volatile setup (`30 04`, `41 00`,
`E0 02`, `E6 19`). It copies sf32rc's exact 799-byte LUTs
and two-stage fast / three-stage quality selector protocol, adapted to400×600.
Transactions have a100-second budget plus35 seconds for reset/native recovery;
refresh must assert BUSY and release within25 seconds. Trial state invalidates
the persistent dashboard render marker before any IO. No OTP programming.

Shared color chart:4px black border; top two columns × three100px rows are
black/white, yellow/red, blue/green; next four50px rows ramp black→each chromatic
ink; bottom100px is a20px checkerboard. Color modes map that source chart to
the original TICKEY palette; declared RGB values are not measured panel colors.
Mono modes use a40px checkerboard after the source's cold clear preparation.

Measured chart comparison, same board/camera, source setup + PaperColor booster:

| Mode | Cold transaction | Settled P4 result |
| --- | ---: | --- |
| Native reference | 16.01 s | Six-color patches and checker visible |
| Color-fast | 7.41 s | Very dark; colors not usable |
| Color-quality | 15.42 s | Very dark; no useful improvement |
| Mono | 3.62 s | Dark/light checker visible, yellowish background |
| MonoClear | 3.80 s | Dark/light checker visible, yellowish background |
| Interrupted native | 1.39 s | Dark checker, strong previous-image residue |

Real diagnostics text rejected both promising monochrome paths: MonoClear
4.71 s produced faint, low-contrast small text; interrupted native2.28 s left
almost unreadable text on a dark field. These were temporary diagnostic builds,
not the final page policy. Evidence: `trial-diagnostics-001` (flash013),
`trial-interrupted-001` (flash014). Final firmware restores native diagnostics.
No fast full-color implementation passed optical acceptance on this panel.

Cold times include native reset/init, source cold clear, frame generation,
drive and controller restoration; they are not warm-update or optical-settle
timings. Evidence under `.cache/papercolor/2026-10-13/`: `trial-native-001`,
`trial-booster-001` (F/Q/M/C, all restored). `trial-fast-001` and
`trial-quality-mono-001` retain native-setup-only baselines; `trial-sf32rc-001`
adds the four source registers before the separate booster comparison.
No LUT byte or guessed voltage sweeps were performed.

`trial-sf32rc-001` captured one delayed USB response: C completed at uptime84s,
but the response tail arrived only after the host's timeout/recovery command.
Native restoration succeeded. An unconditional completion `Serial.flush()`
then passed103 seconds but failed another final status response in
`trial-diagnostics-001`. Arduino2.0.17 HWCDC discards its TX queue when flushing
while disconnected. Runtime now polls the HWCDC connection handshake while
idle and flushes only when connected. `trial-interrupted-001` completed both
trials, native restorations and final status on one connection (uptime41 s).
This is bounded regression evidence, not a long-duration reliability claim.

Final application: `flash-015/manifest.json`, hash-verified app-only write.
`final-native-001` passed native chart → dashboard → native diagnostics →
dashboard with settled JPEG inspection, complete newline-terminated command
acknowledgements and external-power uptime970→70636 ms. Diagnostics17.365 s;
dashboard17.373 s. Final screen is the readable dashboard. Final build and
45 native tests passed; 10 focused host-console tests and changed-Python lint
passed. Battery wake evidence above remains from the earlier power-policy run;
this final pass did not repeat physical unplug.

## LED states

PaperColor RGB LEDs are low-duty heartbeats: brief pulse, off between pulses.

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

## Buttons

- **BtnA** — next page
- **BtnB** — previous page
- **BtnC** — force re-fetch from Axiom now
- **BtnA + BtnB** — diagnostics (battery, wake count, fetch/dedup state)

Repeated taps on the same navigation button within ~500 ms coalesce into a
single page jump and one e-ink refresh. Example: BtnA × 3 advances three pages
with one render (≈ 17 s) instead of three.
The window slides on each new tap, caps at 2 s total, and a BtnC press
during the window cancels nav and triggers a refetch instead. Each press
flashes the LEDs in its color so you know it registered.

A truly simultaneous A+B wake combines the ESP32 EXT1 wake bitmask with an
early snapshot of the live button levels. `EXT1 ANY_LOW` may latch only the
first human press; the other button can become low before runtime ISRs are
installed, leaving no falling edge to count. `setup()` therefore snapshots the
pins before `M5.begin()` while the user is still holding both, then unions that
mask with EXT1. Hold both buttons for about one second when opening
diagnostics. A staggered chord means holding the first button while pressing
the second. To reverse navigation direction, let the current wake cycle finish
before pressing the other button.

## Native tests

```bash
just native-papercolor
```

Covers: colour palette, JSON parser, layout engine. Rendering, WiFi, NVS, and
sleep are device-bound and verified on hardware. The pre-merge hardware plan is
archived in [`../../docs/superpowers/ARCHIVE.md`](../../docs/superpowers/ARCHIVE.md).

## Design + plans

- Current umbrella spec: [`../../docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md`](../../docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md)
- Pre-merge rcpapercolor docs: [`../../docs/superpowers/ARCHIVE.md`](../../docs/superpowers/ARCHIVE.md)
