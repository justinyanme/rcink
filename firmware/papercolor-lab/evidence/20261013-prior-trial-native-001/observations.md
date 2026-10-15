# 20261013-prior-trial-native-001 — native reference chart

**Prior baseline, production firmware port, PaperColor booster.** Driven by
`firmware/papercolor` (`src/dev/waveform_trial.cpp`, M5GFX bus) via
`just trial-papercolor … N`, not by Lab firmware. Archive original:
`.cache/papercolor/rmbp-2026-10-13/2026-10-13/trial-native-001/` (from rmbp).

**Hypothesis**: native OTP transport through the port draws the shared colour
chart readably; gives the cold-transaction reference for F/Q/M/C/I.

**Variables**: mode `N` (native OTP waveform, native init/analog settings). No
custom LUT, no sf32rc registers.

**Setup**: MAC `44:1b:f6:c1:31:90`, then on `/dev/cu.usbmodem20111401` (flash
manifests). Build string `Oct 13 2026 22:46:13`; latest flash before this
Trial by mtime is `flash-010` (inferred, not logged). External power (`flags=0x05`,
VIN 4600 mV), batt 4020 mV, env 31.8 °C / 67 %.

**Sequence** (`events.jsonl`): `p` → `N` → photo → `r` (dashboard restore) →
photo → `p`. One connection.

| Phase (`serial.log`) | ms |
| --- | ---: |
| frame=1 upload | 487 |
| native-on | 131 |
| native-refresh | 14519 |
| native-off | 172 |
| DONE total_ms (cold transaction) | 16013 |
| dashboard re-render `r` push / total | 16914 / 17382 |

`serial.log` shows no `booster=papercolor` line in this Trial (later Trials
print it); README nevertheless groups all chart rows under "source setup +
PaperColor booster".

**Photos**:
- `before.jpg` — dashboard readable; tags (CLAUDE, STALE) look dark/grey rather
  than red.
- `00-N.jpg` — chart: black/white, yellow/red, blue/green blocks distinct;
  ramp rows render as black block + solid chromatic block (no visible
  gradient); bottom checker crisp black/white. `native-rectified.jpg` = 400×600
  rectified crop of the chart (archive PNG re-encoded as JPEG).
- `00-restored.jpg` — dashboard readable, red CLAUDE/STALE tags, green/blue tags.

**Verdict (production README findings table)**: "Native reference | 16.01 s |
Six-color patches and checker visible". Pass as quality/timing reference.
