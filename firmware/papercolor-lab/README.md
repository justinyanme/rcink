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
| `mono`, `monoclear` | reset+init, [setup], cold: PSR+`20` clear_initial, zero frame, power, `12`; then PSR+`20` mono\|mono_clear, `10` transition frame, [power if warm], `12`, `02`, 20 ms; reset+init | `setup=native\|tickey`, `psr=<5F\|0F><69\|A9>`, `booster=native\|none`, `pack=4\|2\|2x` (native values, pack 4) | `setup psr booster pack cold clear_ms busy_ms` |
| `fast`, `quality` | reset+init, [setup], [tres], cold clear (always), PSR+`20` phase1, `10` high selectors, `12`; quality: PSR+`20` phase2, `12` (plane reused); PSR+`20` fast_phase2\|quality_phase3, `10` low selectors, `12`; `02`; fast: `10` high plane post-seed; reset+init | as mono, `pack=4\|4x` only | `setup psr booster tres pack clear_ms p1_ms [p2_ms] final_ms busy_ms` (= phases, without clear) |

`pack`: transition code (old<<1\|new) per pixel at 4 bits (sf32rc), 2 bits (4 px/byte, 60000 B), or `2x`
= 2 bits with each 100-byte row sent twice (120000 B) — the format this panel needs under PSR `0F xx`
(series 3a). `power` = `04` + BUSY; `booster=native` adds 200 ms, `06 6F 1F 17 27`, 200 ms; `none` 10 ms (sf32rc).
`setup=tickey` = `30 04`, `41 00`, `E0 02`, `E6 19` after init. `tres=200` = `61 00 C8 02 58` after init (no effect, series 3b). `psr` = bytes sent with `00` before each LUT.
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

## Own waveforms (route C, from 2026-10-16)

Goal (operator, 2026-10-16): our own waveforms — borrow methods, not TICKEY LUT bytes — first
near black/white mono without ghosting or long-term darkening, then fast colour with six
distinguishable colours, then a 48-colour attempt. Tools: `tools/lutcodec.py` (codec/builder),
`tools/wave_trial.py` (named LUT flows + centre colour), console `lut load|set|get`, mode
`custom` (RAM LUT), image `bands` + `frame=raw` (8 LUT rows side by side), verb `otp`.

### Frame format in LUT mode (decoded)

- `83 00 00 01 8F 00 00 02 57 01` window (knob `win=1`) makes the controller take one data row
  per panel row: mono 2 bpp = 60000 B (`pack=2`), raw 4 bpp = 120000 B (`pack=4`).
- Without the window every panel row consumes two data rows and shows the first
  (`pack=2e` shows the image, `2o` only filler): hence `2x` worked and plain packing squeezed.
- PSR byte 1 selects bit depth: `0F` = 2 bpp, `5F` = 4 bpp. 4 bpp LUT frames cover only the top
  ~491 rows (controller RAM ~96 KiB): full-screen colour must use 2 bpp passes.

### 799-byte LUT format (decoded by BUSY timing + photos)

- Wire byte = bit-inverted value with bits permuted (`lutcodec.SRC`); 19 BUSY timings fit to
  1-2 %. Codec reproduces TICKEY tables byte for byte.
- 7 prefix bytes, 9 rows × 88 B. Row 0 = VCOM (global), row r = pixel code r-1.
- Row = up to 8 groups × 11 B: `[group repeat][level A][level B][A t0 t1 t2][A repeat][B t0 t1 t2][B repeat]`;
  a group without frames ends the row. Frame ≈ 11.7 ms with `setup=tickey` (`30 04`).
- Level byte (raw inverted bits): 5:2 = 4-bit code during t0, 1:0 = code during t1, 7:6 during t2.
- Measured t0 codes (80 frames from native white/black): `1` yellow, `2` black, `8`/`12` white,
  `10` = white+black cancel, `4` none. t1: `1` weak white, `2` yellow, `3` weak white.
  t2: `2`,`3` weak white. Pixel minus VCOM: VCOM `8` with pixel `0` = black (polarity inverted).
- Idle after a drive lets the state relax (black 80 + idle 40 → L 99): rows are right-aligned
  (`lutcodec.right_align`), idle first, every row ends on its own last drive.

### Own mono waveform (2026-10-16)

`tools/wave_trial.py` rows (code×frames): to-white `8x60,1x5,8x20,2x1,8x5` (white, 5-frame yellow
flick, white, 1-frame black, white), to-black `2x85` (charge-balanced with the white frames),
unchanged pixels idle. Own clear: every pixel `opposite, 0x30 rest, target`.

| Measure (centre crop L, camera) | Own fast mono | Native |
| --- | --- | --- |
| white / black | 166–168 / 60–62 | 175 / 55 |
| BUSY / total per update | 1.21 s / ~1.9 s | 14.4 s / 16 s |
| 120 updates (balanced) | white 167, black 61 at 2, 40, 80, 120: no drift | — |
| own clear (ghost removal) | 2.6 s BUSY, removes text ghost | 14.4 s |

Text is black on white (TICKEY tables: navy on yellow). Evidence: `.cache/papercolor-lab/`
`mono-own-001..003`, `long-002`, `rest-001`, `kdur-001`, `white-00x`, `drift-*` (curation pending).

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

### Series 3a — TICKEY mono port audit (#21)

Each Trial: `show white native`, cold `show text mono…`, photo, warm `show checker 40 mono…`, photo, cleanup.
Times: cold clear / mono BUSY / total; warm BUSY / total (total includes the reset + native init around
every LUT drive).

| Trial | Variable | Times (ms) | Result |
| --- | --- | --- | --- |
| [mono-001](evidence/20261015-mono-001/observations.md) | baseline (native knobs) | 4933/244/6728; 267/1568 | squeezed to top half, pinkish |
| [mono-002](evidence/20261015-mono-002/observations.md) | `setup=tickey` | 1915/94/3507; 117/1367 | same geometry; 2.6× faster frames |
| [mono-003](evidence/20261015-mono-003/observations.md) | `psr=0FA9` | 4966/275/6789; 298/1599 | 2× horizontal stretch, striped columns |
| [mono-004](evidence/20261015-mono-004/observations.md) | `booster=none` | 4935/244/6337; 267/1178 | identical to 001 |
| [mono-005](evidence/20261015-mono-005/observations.md) | sf32rc set (setup+psr, no booster) | 1927/106/3140; 129/988 | stretched + striped, pale |
| [mono-006](evidence/20261015-mono-006/observations.md) | 10-13 port set (setup+psr+booster) | 1927/106/3530; 129/1378 | as 005: the 10-13 "dark" class |
| [mono-007](evidence/20261015-mono-007/observations.md) | `pack=2` (vs 005) | 1927/106/3002; 129/850 | stripes gone; image in top half, 40×20 squares |
| [mono-008](evidence/20261015-mono-008/observations.md) | `psr=0F69` | 4936/244/6728; 267/1567 | stretched + striped: PSR byte 1 sets format |
| [mono-009](evidence/20261015-mono-009/observations.md) | `psr=5FA9` | 4966/275/6789; 298/1598 | squeezed, but navy on yellow: byte 2 = contrast |
| [mono-010](evidence/20261015-mono-010/observations.md) | `pack=2x` (vs 007) | 1927/106/3126; 129/974 | **correct geometry**, readable; warm ghost |
| [mono-011](evidence/20261015-mono-011/observations.md) | `booster=native` (vs 010) | 1927/106/3516; 129/1364 | same as 010 |
| [mono-012](evidence/20261015-mono-012/observations.md) | `setup=native` (vs 010) | 4967/275/6386; 298/1194 | darker, slower; warm ghost |
| [mono-013](evidence/20261015-mono-013/observations.md) | `monoclear` (vs 010) | 1927/294/3314; 317/1162 | **crisp navy on yellow, no warm ghost** |
| [mono-014](evidence/20261015-mono-014/observations.md) | 013 + 10 × invert | warm 317/1166–1169 each | **pass**: clean after 1/5/10; Cleanup recovers |
| [mono-015](evidence/20261015-mono-015/observations.md) | `trace on`, cold + warm | 294/3315; 316/1172 | transcript below |

**Verdict per hypothesis** (the 10-13 "dark output"):

| Hypothesis | Verdict | Decided by |
| --- | --- | --- |
| Pixel packing / plane format | **Confirmed cause.** Under PSR `0F xx` the controller reads 2 bits per pixel and two data rows per panel row; sf32rc's 4-bit JD7601 packing gives stretched, striped, pale output | 003, 007, 008, 010 |
| PSR bytes | Byte 1 (`5F`→`0F`) switches the frame format (needed for the LUT path's geometry with `pack=2x`); byte 2 (`69`→`A9`) raises contrast | 003, 008, 009 |
| Booster sequence | Ruled out (no visible change, +390 ms) | 004, 011 |
| Volatile setup (`30 04, 41 00, E0 02, E6 19`) | Not the cause; sets frame timing (clear 4.9→1.9 s, mono 275→106 ms) and darkness | 002, 012 |
| Command order | Ruled out: LUT→frame→`04`→`12`→`02` as sf32rc; correct output once packing fixed | 010, 015 |
| Old/new plane preparation | Ruled out: cold all-zero history and warm old plane both give correct transitions (014 clean after 10 inversions); `mono` (vs `monoclear`) LUT ghosts on warm updates — a LUT property | 010, 013, 014 |
| Why 2 rows per panel row | **Open.** Matches a dual-gate / 800-source controller layout; `2x` is an empirical format. Next Trial: send alternate rows (row y, then a white row) to see which copy the panel shows | — |

**Fast mono status**: `monoclear setup=tickey psr=0FA9 pack=2x booster=none` meets the Fast mono thresholds
(warm BUSY 317 ms, 1.17 s total incl. reset/init; 16 px readable incl. 13 pt; 10 updates then Cleanup fully
recovers). Light is yellow and dark navy, not white/black (as sf32rc's JD7601 "dark-on-yellow").

`trace on` transcript (mono-015, cold `monoclear`) vs sf32rc `tickey_mono.c` cold path:

| Lab | sf32rc | Difference |
| --- | --- | --- |
| RST 1/2/10 ms, native list (`AA…`, `00 5F 69`, booster `06…17`, `30 08`, `50 3F`, `61 01 90 02 58`) | RST 20/20 ms + idle, `E9 01` | JD7601 vs ED2208 init: kept native |
| `30 04`, `41 00`, `E0 02`, `E6 19` | same | — |
| `00 0F A9`, `20` ×799 clear_initial | same | — |
| `10` ×120000 zeros | `10` ×28800 zeros | panel size |
| `04` (+10 ms) | `04` (+10 ms) | — |
| `12 00` | `12 00` | — |
| `00 0F A9`, `20` ×799 mono_clear | same | — |
| `10` ×120000 transition, **2 bpp, rows twice** | `10` ×28800, 4 bpp | **packing (the cause)** |
| `12 00`, `02 00` | `12 00`, `02 00` | — |
| reset + native list | `07 A5` deep sleep | Lab restores native state |

Warm: `30…E6` setup, `00 0F A9`, `20`, `10`, `04`, `12 00`, `02 00` — same order as sf32rc.

### Series 3b — TICKEY staged colour audit (#22)

Each Trial: `show white native`, one `show chart|text fast|quality …`, photo, restore cleanup + photo.

| Trial | Variable | BUSY / total (ms) | Result |
| --- | --- | --- | --- |
| [color-001](evidence/20261015-color-001/observations.md) | baseline (native knobs) | 2532 / 9634 | squeezed to top half, dark pink-brown; 0/6 colours |
| [color-002](evidence/20261015-color-002/observations.md) | setup+`psr=0FA9`+no booster+`pack=4x` | 999 / 5673 | 2 bpp read: stretched, striped, dark |
| [color-003](evidence/20261015-color-003/observations.md) | setup+`psr=5FA9`+no booster+`pack=4x` | 999 / 5672 | right scale, **wrapped at ~200000 B**; yellow, dark-yellow (red), light blue, olive |
| [color-004](evidence/20261015-color-004/observations.md) | `pack=4` (vs 003) | 999 / 4652 | squeezed to top half; same colours |
| [color-005](evidence/20261015-color-005/observations.md) | `tres=200` (vs 004) | 1001 / 4652 | identical to 004: 0x61 width ruled out |
| [color-006](evidence/20261015-color-006/observations.md) | `setup=tickey` (vs 001) | 977 / 5006 | as 001, faster |
| [color-007](evidence/20261015-color-007/observations.md) | `psr=0FA9` (vs 001) | 2597 / 9730 | stretched + striped |
| [color-008](evidence/20261015-color-008/observations.md) | `booster=none` (vs 001) | 2534 / 9243 | as 001 |
| [color-009](evidence/20261015-color-009/observations.md) | `quality` with 003 knobs | 10271 / 14404 | wrapped; more saturated, still no red |
| [color-010](evidence/20261015-color-010/observations.md) | `text fast` with 003 knobs | 1001 / 5598 | wrapped; black on light grey, 16–24 pt readable |
| [mono-016](evidence/20261015-mono-016/observations.md), [mono-017](evidence/20261015-mono-017/observations.md) | mono regression; `tres=200` | 294 / 3318; 295 / 3065 | regression pass; `tres` ruled out for mono too |

Every Cleanup photo restores the native chart/text. Draft color verdict: **not met** — layout (wrap) and
palette (no red; white→light grey) both fail; quality BUSY 10.3 s also exceeds 6 s.

## Synthesis (routes A/B, 2026-10-15)

| Series | Best configuration | vs thresholds |
| --- | --- | --- |
| 1 Native | `native` | reference: 14.4 s BUSY, six colours, all text |
| 2 Interrupted OTP (route A, FreeInk) | `interrupt cut=340 cdi=0F` | Fast mono numbers met (0.78 s, 13 pt, Cleanup recovers); light = pale yellow; unbalanced waveform (community reports darkening) → reference only |
| 3a TICKEY mono (route B) | `monoclear setup=tickey psr=0FA9 pack=2x booster=none` | **Fast mono met**: warm BUSY 317 ms, 1.17 s incl. reset/init, 13 pt readable, 10 updates clean, Cleanup recovers; light = yellow, dark = navy |
| 3b TICKEY colour (route B) | `fast setup=tickey psr=5FA9 pack=4x booster=none` | **Draft color not met**: frame wraps, no red, grey white |

Promising: TICKEY `monoclear` with the corrected packing is a balanced-LUT Fast mono candidate for
integration (later spec), pending a long-run darkening check. Ruled out as causes of the 10-13 failure:
booster, command order, plane preparation, setup registers (they set speed), resolution width.

Open hypotheses, each with its next single-variable Trial:

| Hypothesis | Next Trial |
| --- | --- |
| LUT-mode layout: which of the two data rows per panel row is displayed (gate pairing) | mono `pack=2` variant sending row y then a white row (vs `2x`); then the reverse |
| 4 bpp colour frame exceeds controller RAM (~200000 B observed) | `fast` sending only the displayed row of each pair as 4 bpp (120000 B) once the pairing is known |
| Colour selector → ink mapping differs on EL040EF1 (red patch renders dark yellow) | palette-grid image: one patch per legal selector pair, `fast psr=5FA9`, identify which codes give red/white |
| PSR byte 2 (`69` vs `A9`) role in colour | `fast psr=5F69 pack=4x` vs 003 |
| Long-term DC balance of `monoclear` | 200 warm updates loop with photos every 50, then Cleanup |

**Route C (voltage/register tuning): not justified yet.** Every failure so far traced to data format
(packing, line pairing, RAM size) rather than drive strength; the open items above are all format/palette
Trials that need no voltage change. Decision for the operator: keep route C closed until the layout and
palette Trials are exhausted.

### Photo index by mode

Representative photo per mode (all under `evidence/20261015-*/`):

| Mode / knobs | Trial | Time | What the panel shows |
| --- | --- | --- | --- |
| `native` | native-baseline-001, -002 | BUSY ~14.4 s, total ~16 s | six colours, crisp text, true black/white (reference) |
| `interrupt cut=340` | interrupt-002 | 0.78 s total | navy text on pale yellow-white, all sizes readable |
| `interrupt cut=250 / 450` | interrupt-001 / -003 | 0.69 / 0.89 s | background yellow-green / pale blue: cut picks the hue |
| `interrupt cdi=3F` | interrupt-005 | 0.78 s | polarity inverted (pale text on navy) |
| `interrupt` ×10 invert | interrupt-006 | 0.78 s each | light→yellow, dark→navy, no ghost; Cleanup recovers |
| `mono` sf32rc as-is (pack 4) | mono-001, -003, -005, -006 | 1–7 s | squeezed to top half, or 2× stretched + striped, grey/pale (= the 10-13 failure) |
| `mono pack=2x` | mono-010 | cold 3.1 s, warm 0.97 s | full panel, blue on yellow; warm update ghosts |
| `monoclear pack=2x` | mono-013, -014 | cold 3.3 s, warm 1.17 s (BUSY 317 ms) | crisp navy on yellow, no ghost after 10 updates; Cleanup recovers |
| `fast` / `quality` `pack=4x` | color-003, -009 | BUSY 1.0 / 10.3 s | right scale but wrapped; red→dark yellow, white→grey |
| `fast` text | color-010 | BUSY 1.0 s | black on light grey, readable, wrapped |

### Learnings (2026-10-15)

1. The 10-13 "dark output" was **pixel packing**: under PSR `0F xx` (LUT path) this controller takes 2 bpp
   codes and two data rows per panel row; sf32rc's 4 bpp JD7601 layout gives stretched/striped/pale output.
2. PSR byte 1 (`5F`→`0F`) switches frame format (4 bpp → 2 bpp); byte 2 (`69`→`A9`) raises contrast.
3. In LUT mode every panel row consumes two data rows at any bit depth (2x/4x); `61` width does not change it.
4. Controller frame RAM ~200000 B in this mode: a 4 bpp doubled frame (240000 B) wraps by ~100 rows.
5. TICKEY setup `30 04, 41 00, E0 02, E6 19` changes timing only (clear 4.9→1.9 s, mono 275→106 ms).
6. Booster `06 6F 1F 17 27`: no visible effect in any LUT or interrupt Trial.
7. `mono` LUT ghosts on warm updates; `monoclear` does not.
8. LUT light colour is yellow, dark is navy (same as sf32rc on JD7601); not black/white.
9. CDI `50` bits 5:4 = data polarity; FreeInk's interrupted drive needs `0F`.
10. Interrupted OTP: cut must anchor on observed BUSY assert; cut time selects background hue (250/340/450 ms).
11. Native BUSY is fixed per temperature (run-to-run ≤2 ms; colder = longer: 14506 ms at `1e00`, 14387 at `1f80`).
12. TSC `40` read-back likely °C (byte 0) + 0.5 °C (byte 1 bit 7); not thermometer-verified.
13. The 10-13 interrupt failure was not the method: Lab interrupt (with `83` window, no 400 ms delays) is readable.

### Why TICKEY works on DayoTag but colour does not (yet) here

TICKEY's LUTs and palette were made for DayoTag V2: JD7601 controller, 1.54" 240×240 panel. There
sf32rc measured 48/55 distinguishable fast-colour slots and multicolour postcards in 3.97 s
(sf32rc `docs/dayotag-v2-modes.md`); its mono is dark-on-yellow there too — our `monoclear` now matches it.
Differences on PaperColor (ED2208, 4" EL040EF1 400×600):

| Difference | Effect here | Status |
| --- | --- | --- |
| Controller: ED2208 vs JD7601 (TICKEY init is `E9 01` only) | LUT-mode frame format differs (2 bpp, rows twice) | solved for mono, open for 4 bpp colour |
| Frame size: 28800 B vs 120000–240000 B | 4 bpp doubled frame exceeds controller RAM → wrap | open: next Trial = send only the displayed row of each pair |
| Panel film / size | LUT voltages+timings tuned to TICKEY's panel; selector→colour slots measured on DayoTag | open: red → dark yellow, white → grey; next Trial = palette grid of all selector pairs |
| Init context: native ED2208 list + TICKEY setup regs | register meanings unknown on ED2208 | setup = speed only; rest open |
| Temperature (`E6 19` = 25?) | LUT not compensated | not tested |

So "TICKEY is good" holds for its own controller+panel; on this panel the same bytes need a different frame
layout (found for mono) and probably a different selector palette (colour). LUT voltages themselves are
route C (not opened).

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
