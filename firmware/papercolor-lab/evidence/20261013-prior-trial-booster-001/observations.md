# 20261013-prior-trial-booster-001 — sf32rc F/Q/M/C with PaperColor booster

**Prior baseline, production firmware port, PaperColor booster.** Driven by
`firmware/papercolor` (`src/dev/waveform_trial.cpp`) via
`just trial-papercolor … FQMC`, not Lab firmware. Archive original:
`.cache/papercolor/rmbp-2026-10-13/2026-10-13/trial-booster-001/`.
`trial-booster-001.log` = host session log (photo size/sharpness lines + serial).

**Hypothesis**: sf32rc's 799-byte RAM LUTs (color-fast, color-quality, mono,
mono-clear), with sf32rc volatile setup plus PaperColor post-power-on booster,
give usable fast output on EL040EF1.

**Variables**: mode F/Q/M/C (LUT + selector protocol). Fixed: sf32rc regs
`30:04,41:00,E0:02,E6:19`; booster `06 6F 1F 17 27`. Compared to
`trial-sf32rc-001` (same regs, no booster line) — this Trial adds the booster.

**Setup**: MAC `44:1b:f6:c1:31:90`, port then `/dev/cu.usbmodem20111401`.
Build string `Oct 13 2026 22:46:13`; latest flash before by mtime `flash-012`
(inferred). External power (`0x05`), batt 4076→4086 mV, env 31.6 °C / 66 %.

**Sequence**: `p` → F → photo → `r` → photo → Q → … → C → photo → `r` → photo
→ `p`. One connection, all four `native_restored=1`.

| Mode | clear-initial | colour/mono phases (drive_ms) | total_ms |
| --- | ---: | --- | ---: |
| F color-fast | 1929 | phase1 895, final 107, off 171 | 7408 |
| Q color-quality | 1930 | phase1 4532, phase2 5651, final 107, off 172 | 15417 |
| M mono | 1930 | mono 107, off 172 | 3616 |
| C mono-clear | 1930 | mono-clear 295, off 172 | 3804 |

Every native dashboard restore: push 16905–16914 ms, total 17373–17382 ms.

**Photos**:
- `00-F.jpg` — whole panel dark grey; chart blocks only faintly separable;
  checker barely visible; no usable colour.
- `01-Q.jpg` — similarly dark; faint reddish/teal tints in a few blocks and a
  dim red/dark checker; no usable colour.
- `02-M.jpg`, `03-C.jpg` — 40 px checker clearly visible, dark squares
  blue-grey, light squares yellowish/beige (not white).
- `*-restored.jpg`, `before.jpg` — dashboard readable after every mode.

**Verdict (production README findings table)**: Color-fast 7.41 s "Very dark;
colors not usable"; Color-quality 15.42 s "Very dark; no useful improvement";
Mono 3.62 s and MonoClear 3.80 s "Dark/light checker visible, yellowish
background". Physical comparison rejected every experimental mode for normal
pages. Cold times include native reset/init, cold clear, frame generation,
drive and restoration — not warm-update or optical-settle times.
