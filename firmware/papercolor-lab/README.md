# papercolor-lab

Lab firmware for the M5Stack PaperColor: drives only the panel from a line
console, with every controller byte written by this repo (no M5GFX/M5Unified).
Boots to an idle console in ~250 ms and paints nothing until told to. Spec and
vocabulary: issue #15, `CONTEXT.md` (Native refresh, Trial, Lab firmware).

```bash
just native-papercolor-lab                       # host tests (protocol, encoder, init table)
just build-papercolor-lab
just flash-papercolor-lab /dev/cu.usbmodem83301 .cache/papercolor-lab/flash-NNN
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
| `show <white\|black> native` | full field, native OTP → `OK image= mode= busy_ms= total_ms=` |
| `raw <cmd> [data…]` | one command verbatim (≤64 bytes) → `OK cmd= len=` |
| `rawread <cmd> <1..32>` | → `OK cmd= data=<hex>` |
| `trace <on\|off>` | echo every controller write as `# write cmd=XX len=N` → `OK trace=` |
| `help` | `OK verbs=… show=… raw=… rawread=… trace=…` |
| anything else | `FAIL stage=parse reason=<empty\|verb\|args\|image\|mode\|hex\|count\|arg\|length>` |

A drive failure is `FAIL stage=<power-on-assert|power-on-release|drive-assert|drive-release|power-off-release|frame-idle|init-idle|…> busy=<level> ms=<waited>`.
`busy_ms` is the BUSY-asserted duration of the `12` refresh; `total_ms` spans
frame upload through power-off. `temp` is the raw TSC (`0x40`) read-back; its
decoding is not established (cold `20 80`, after two drives `21 00`).

The console flushes USB CDC only while the host handshake reports connected
(Arduino 2.0.17 HWCDC discards the queue otherwise). Images other than
`white`/`black`, and modes other than `native`, arrive with #19–#22.

## Module layout

- `protocol.{h,cpp}` — pure parser + `Reply` formatter (host-tested).
- `frame.{h,cpp}` — pure native encoder (host-tested).
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
