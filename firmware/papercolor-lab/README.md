# papercolor-lab

Lab firmware for the M5Stack PaperColor: drives only the panel from a line
console, with every controller byte written by this repo (no M5GFX/M5Unified).
Boots to an idle console in ~250 ms and paints nothing until told to. Spec and
vocabulary: issue #15, `CONTEXT.md` (Native refresh, Trial, Lab firmware).

```bash
just native-papercolor-lab                       # firmware host tests (protocol, encoder, init table)
just lab-host-test                               # lab.py against recorded transcripts
just build-papercolor-lab
just flash-papercolor-lab /dev/cu.usbmodem83301 .cache/papercolor-lab/flash-NNN
just lab-trial /dev/cu.usbmodem83301 android:3B161600Z2U00000 .cache/papercolor-lab/NAME "show black native,photo"
```

Flashing uses `scripts/papercolor_flash.py --build-dir …`: same MAC check,
same partition-table comparison, app0 only. Production firmware returns with
`just flash-papercolor`. Opening the USB console resets the board; wait for the
`# ready` banner before the first command.

## Bring-up sources

Every value is copied from the pinned libraries in `../papercolor/platformio.ini`
(M5GFX `5268353`), cited in the source:

| Item | Value | Source |
| --- | --- | --- |
| PM1 I2C | SDA 3, SCL 2, 100 kHz, addr 0x6E, id reg 0x00..01 = 0x2050 | `M5GFX.cpp` 165-187, 1834-1845 |
| PM1 EPD power | 0x0A=0; 0x16 &= ~0xC3; 0x10 \|= 0x09; 0x13 &= ~0x09; 0x11 \|= 0x09; 0x09=0; 100 ms | `M5GFX.cpp` 1856-1869 (`board.cpp`) |
| Panel CS / SD CS | GPIO44 high, GPIO47 high + SD CMD0 handshake | `M5GFX.cpp` 1871-1872, 1886, 734 |
| SPI | MOSI 13, MISO 14, SCLK 15, DC 43, mode 0, 3-wire, 4 MHz write | `M5GFX.cpp` 1874-1883 |
| RST / BUSY | GPIO12 (1 ms high, 2 ms low, 10 ms high); GPIO11 pull-up, low = busy | `M5GFX.cpp` 711, 1873, 1894 |
| Init | CMDH, PSR `00 5F 69`, booster `06 6F 1F 17 17`, …, `61 01 90 02 58` | `Panel_ED2208.hpp` list0 + `_init_sequence` (`epd_init.h`) |
| Refresh | `04`, wait, 200 ms, `06 6F 1F 17 27`, 200 ms, `12 00`, wait, `02 00`, wait, 200 ms | `Panel_ED2208.cpp` `_turn_on_display` |
| Frame | `10` + 120000 bytes, two ink codes per byte, left pixel high nibble | `_exec_transfer`, `_dither_row_none` |

Deviations from the library, deliberate: CS toggles per command instead of per
transaction; register reads are bit-banged on MOSI (LGFX SIO half-duplex) while
the hardware SPI is released; BUSY waits are bounded (assert 1 s, release 25 s)
instead of 20 s with silent continue. PM1 absent, I2C NACK, init BUSY timeout,
register 0x70 all-zero/all-FF or PSRAM allocation failure abort with
`[boot] fatal: …`. Nothing in this firmware writes OTP or changes
voltage/power registers beyond the pinned values.

## Console protocol

Newline-terminated, space-separated, verb first. Exactly one reply line per
request: `OK key=value …` or `FAIL key=value …`. Lines starting `# ` are
progress/trace and carry no result. Hex bytes are exactly two digits, no prefix.

| Request | Reply |
| --- | --- |
| `info` | `OK mac= rev=<0x70, 4 bytes> build= temp=<0x40, 2 bytes raw> uptime_ms=` |
| `reset` | hard reset + native init, no paint → `OK total_ms=` |
| `cleanup` | Native refresh of the current image → `OK mode=native busy_ms= total_ms=`; before any `show`: `FAIL stage=state reason=no-image` |
| `show <image> [N] <mode> [knob=v…]` | render image, drive in mode → `OK image= [size=] mode= <mode fields> busy_ms= total_ms=` (see Modes) |
| `raw <cmd> [data…]` | one command verbatim (≤64 bytes) → `OK cmd= len=` |
| `rawread <cmd> <1..32>` | → `OK cmd= data=<hex>` |
| `trace <on\|off>` | echo every controller write as `# write cmd=XX len=N` → `OK trace=` |
| `help` | `OK verbs=… show=… raw=… rawread=… trace=…` |
| anything else | `FAIL stage=parse reason=<empty\|verb\|args\|image\|mode\|hex\|count\|arg\|length>` |

A drive failure is `FAIL stage=<power-on-assert|power-on-release|drive-assert|drive-release|power-off-release|frame-idle|init-idle|…> busy=<level> ms=<waited>`.
`busy_ms` is the BUSY-asserted duration of the `12` refresh; `total_ms` spans
frame upload through power-off. `temp` is the raw TSC (`0x40`) read-back; its
decoding is not established. Observed `1e00`, `1f80`, `2000`, `2080`, `2100`:
consistent with first byte = °C and bit 7 of the second = 0.5 °C (bench ~30–33 °C),
unconfirmed against a thermometer.

Images (`image.{h,cpp}`, 400×600 ink plane, one byte per pixel):

| Image | Content |
| --- | --- |
| `white`, `black` | full field |
| `chart` | 2026-10-13 chart: 4 px black border; 200×100 patches black\|white, yellow\|red, blue\|green; y 300–499 four 50 px black→ink RGB ramps (yellow, red, blue, green) quantised to nearest ink (same anchors as production `waveform_trial.cpp`); y≥500 20 px checker |
| `text` | VT323 13/16/18/24 pt (production headers via `src/shim/M5GFX.h`), `A–Z a–z 0–9` black on white wrapped at 8 px margins; one 16 pt white-on-black line |
| `checker N` | N px squares (1–300), (0,0) black |
| `invert` | current image with black↔white; chromatic inks kept; `FAIL stage=state reason=no-image` before any show |

The console flushes USB CDC only while the host handshake reports connected
(Arduino 2.0.17 HWCDC discards the queue otherwise).

## Modes (`modes.cpp`)

| Mode | Sequence | Knobs (default) | Fields |
| --- | --- | --- | --- |
| `native` | `10` frame, `04`, 200 ms, `06 6F1F1727`, 200 ms, `12 00`, 200 ms, `02 00`, 200 ms (Panel_ED2208 `_turn_on_display`) | — | `busy_ms` |
| `interrupt` | `50 cdi`, `10` frame (native codes), `04`, 20 ms, `06 6F1F1727`, `83 00 00 01 8F 00 00 02 57 01`, `50 cdi`, `12 00`, BUSY assert, `cut` ms, RST low 2 ms / high 10 ms, native init (no `02`) | `cut=340` (50–5000), `cdi=0F\|3F` (0F) | `cut_ms cdi busy_ms` (assert→cut) |
| `mono`, `monoclear` | reset+init, [setup], cold: PSR+`20` clear_initial, zero frame, power, `12`; then PSR+`20` mono\|mono_clear, `10` transition frame, [power if warm], `12`, `02`, 20 ms; reset+init | `setup=native\|tickey`, `psr=5F69\|0FA9`, `booster=native\|none` (native values) | `setup psr booster cold clear_ms busy_ms` |
| `fast`, `quality` | reset+init, [setup], cold clear (always), PSR+`20` phase1, `10` high selectors, `12`; quality: PSR+`20` phase2, `12` (plane reused); PSR+`20` fast_phase2\|quality_phase3, `10` low selectors, `12`; `02`; fast: `10` high plane post-seed; reset+init | as mono | `setup psr booster clear_ms p1_ms [p2_ms] final_ms busy_ms` (= phases, without clear) |

`power` = `04` + BUSY; `booster=native` adds 200 ms, `06 6F 1F 17 27`, 200 ms; `none` 10 ms (sf32rc).
`setup=tickey` = `30 04`, `41 00`, `E0 02`, `E6 19` after init. `psr` = bytes sent with `00` before each LUT.
LUT tables: unmodified sf32rc `ce0a01a` `tickey_waveforms_{mono,color}.inc` (`waveforms.h`). Mono old plane
(1 bpp, light = white\|yellow) lives in PSRAM; invalidated by boot, `reset`, `cleanup`, any native/interrupt/
colour drive and any failure. Experimental modes leave the controller reset + natively initialised.

### Interrupt: semantics (FreeInk `Free-Ink/freeink-sdk` e91e34e = 3af4166, `libs/display/FreeInkDisplay/src/driver/Ed2208M5Driver.cpp`)

- Plane: one `10` frame, full 400×600 (no window narrowing; `83` covers the whole panel), 4 bpp native codes,
  left pixel high nibble; white `1`, black `0` (`.cpp:136-137`); chromatic inks are not used by FreeInk's
  fast path. Cut anchored on the observed BUSY assertion, not the command send (`.cpp:238-257`).
- CDI `50 0F` (FreeInk "dark hack") vs native `3F`: bits 5:4 set data polarity for this drive
  (`20261015-interrupt-005`).
- Community observation (FreeInk README.md:213-221): interrupted waveforms are not DC-balanced; over hours of
  interrupted-only use the panel darkens and colours fade; FreeInk promotes a complete waveform about hourly.
  Not reproduced here (Trials are minutes long).

## Acceptance thresholds

Fixed before any experimental mode (spec #15); a change needs the data that
motivated it, recorded here.

| Target | Threshold |
| --- | --- |
| Fast mono | warm BUSY ≤2 s; 16 px text readable; 10 updates then Cleanup fully recovers |
| Draft color | BUSY ≤6 s; six colours distinguishable; 16 px black-on-white text readable |
| Both | Native refresh afterwards looks normal |

Native baseline (series 1, temp `1e00`–`2000`): BUSY 14387–14509 ms, total
16128–16333 ms per drive; run-to-run ≤2 ms at a given image; chart six colours
distinguishable; all text sizes incl. 13 pt readable.

## Host script `lab.py`

PEP 723 typer CLI (`uv run firmware/papercolor-lab/lab.py --help`):

- `trial OUT --port P --camera android:SERIAL --steps "show black native,photo,…" [--no-restore] [--step-timeout 60] [--flash]`
  — opens `P` once (DTR/RTS inactive, exclusive), waits for `# ready` (opening
  resets the unit; 5 s), sends each step and waits for its one `OK`/`FAIL`
  line. `photo` runs `uv run --project ../rceye eye --eye CAMERA photo --out
  OUT/NN-<previous step>.jpg`. If anything was shown, ends with `cleanup` +
  photo unless `--no-restore`. `--flash` first runs `flash OUT-flash`.
- `flash OUT --port P` — wraps `scripts/papercolor_flash.py` with the Lab build dir.
- `curate SRC DEST` — Trial dir → evidence dir: `serial.log`, `events.jsonl`
  verbatim, JPEGs ≤1600 px long edge and ≤200 KB.

Fail loud, no retry: absent port, existing `OUT`, missing banner, `[boot]
fatal:`/Guru/abort, short write, no result within the step timeout, a `FAIL`
result, or the port vanishing each stop the session. After a failure on a live
port with something shown, exactly one best-effort `cleanup` + photo
(`NN-cleanup-after-failure.jpg`); the original error is still raised.

Trial directory (`OUT`, must not exist):

| File | Content |
| --- | --- |
| `serial.log` | raw bytes from open to close |
| `events.jsonl` | `{t_ns, clock: host-monotonic, event, …}`; events `open`, `boot`, `write` (line), `result` (line, ok, fields), `photo` (file, camera, stdout), `error` (message) |
| `NN-<step-slug>.jpg` | camera originals, numbered in capture order |

## Module layout

- `protocol.{h,cpp}` — pure parser + `Reply` formatter (host-tested).
- `frame.{h,cpp}` — pure native encoder (host-tested).
- `image.{h,cpp}` — pure image renderers + GFXfont glyph renderer (host-tested); `fonts.h` + `shim/M5GFX.h` reuse the production VT323 headers.
- `epd_init.h` — pinned byte tables (host-tested against a second copy).
- `epd.{h,cpp}` — driver: `begin, hard_reset, init_native, upload_frame, upload_lut, power_on, drive, power_off, read_register`; all writes through `write()`.
- `board.{h,cpp}` — PM1 bring-up, CS idle levels. `main.cpp` — console loop.
- `build_hash.py` — embeds `git rev-parse --short=12 HEAD` (+`-dirty`) as the build id.

## Evidence

`evidence/<YYYYMMDD>-<series>-<nnn>/` holds `serial.log`, `events.jsonl`,
JPEGs ≤1600 px / ≈≤200 KB and `observations.md`. Originals stay in gitignored
`.cache/papercolor-lab/`.

| Trial | Verdict |
| --- | --- |
| [20261015-skeleton-001](evidence/20261015-skeleton-001/observations.md) | **Pass.** Raw driver: `show black native` then `show white native`, each 14389 ms BUSY / 16137 ms total, whole panel uniform in both photos; 0x70 = `01010400`. |
| [20261015-hostscript-001](evidence/20261015-hostscript-001/observations.md) | **Pass.** `lab.py trial` end to end: one open, banner, 5 results, 2 photos, restore cleanup; black field, `busy_ms` 14389 again. |

### Series 1 — Native refresh baseline (#19)

| Trial | Verdict |
| --- | --- |
| [20261015-native-baseline-001](evidence/20261015-native-baseline-001/observations.md) | **Reference.** `chart`: BUSY 14506 / total 16333 ms; six colours distinguishable, ramps and checker crisp; layout matches 10-13. |
| [20261015-native-baseline-002](evidence/20261015-native-baseline-002/observations.md) | **Reference.** `text`: BUSY 14387 / 16142 ms; 13–24 pt and white-on-black 16 pt readable. |
| [20261015-native-baseline-003](evidence/20261015-native-baseline-003/observations.md) | **Reference.** `text` repeat: BUSY 14387 / 16142 ms; run-to-run ≤2 ms. |

### Series 2 — interrupted-OTP mono reference (#20)

Each Trial: `show white native`, one interrupted drive, photo, restore cleanup. Build `edd95fbd3537`.

| Trial | Verdict |
| --- | --- |
| [20261015-interrupt-001](evidence/20261015-interrupt-001/observations.md) | `text cut=250`: 685 ms; navy on yellow-green; all sizes readable. |
| [20261015-interrupt-002](evidence/20261015-interrupt-002/observations.md) | **Pass.** `text cut=340`: 775 ms; navy on pale yellow-white; all sizes readable. |
| [20261015-interrupt-003](evidence/20261015-interrupt-003/observations.md) | `text cut=450`: 885 ms; navy on pale blue-white; all readable. Cut selects background hue. |
| [20261015-interrupt-004](evidence/20261015-interrupt-004/observations.md) | `checker 40`: 772 ms; blue/cream squares, uniform, no seam. |
| [20261015-interrupt-005](evidence/20261015-interrupt-005/observations.md) | `cdi=3F`: polarity inverted (pale text on navy). CDI 0F required. |
| [20261015-interrupt-006](evidence/20261015-interrupt-006/observations.md) | **Pass.** 10 × `invert`: light → yellow, dark → navy, no ghosting; Cleanup fully recovers. |

**Series verdict vs Fast mono thresholds**: ≤2 s ✓ (0.69–0.89 s total, no separate warm path); 16 px
readable ✓ (13 pt too); 10 updates then Cleanup recovers ✓. Not white: light is pale yellow/blue by cut,
dark is navy, not black. **This mode is the same-panel reference, not a product candidate**: it relies on
an unbalanced, cut waveform (FreeInk reports long-term darkening). The 10-13 port's "almost unreadable"
result (prior-trial-interrupted-001) is not a property of the method; candidates for the difference: the
`83` full window, 400 ms of power-on delays, CDI handling, and the dashboard history.

### Prior baseline (production firmware port, PaperColor booster, 2026-10-13)

Not Lab Trials: driven by `firmware/papercolor` `src/dev/waveform_trial.cpp`
(M5GFX bus, PaperColor booster `06 6F 1F 17 27`) via `just trial-papercolor`.
Times are cold transactions (reset/init, clear, frame, drive, restore), not
Lab `busy_ms`. Verdicts quoted from `../papercolor/README.md` findings. Full
archive (from rmbp): gitignored `.cache/papercolor/rmbp-2026-10-13/`.

| Trial | Verdict |
| --- | --- |
| [20261013-prior-trial-native-001](evidence/20261013-prior-trial-native-001/observations.md) | **Reference.** Native chart 16.01 s (refresh phase 14519 ms); six-colour patches and checker visible. |
| [20261013-prior-trial-booster-001](evidence/20261013-prior-trial-booster-001/observations.md) | **Fail.** sf32rc LUTs: F 7.41 s / Q 15.42 s very dark, colours unusable; M 3.62 s / C 3.80 s checker visible, yellowish background. |
| [20261013-prior-trial-interrupted-001](evidence/20261013-prior-trial-interrupted-001/observations.md) | **Fail.** 340 ms interrupted native: chart 1.39 s dark checker + strong residue; diagnostics 2.28 s almost unreadable on dark field. |
| [20261013-prior-final-native-001](evidence/20261013-prior-final-native-001/observations.md) | **Pass.** Final app `flash-015`: native chart 16.01 s, dashboard 17.373 s, diagnostics 17.365 s, all readable; uptime 970→70636 ms. |
