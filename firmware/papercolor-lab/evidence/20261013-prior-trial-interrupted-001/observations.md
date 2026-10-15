# 20261013-prior-trial-interrupted-001 — 340 ms interrupted native mono

**Prior baseline, production firmware port, PaperColor booster.** Driven by
`firmware/papercolor` temporary diagnostics build (`flash-014`, per production
README) via `just trial-papercolor … Id`, not Lab firmware. Archive original:
`.cache/papercolor/rmbp-2026-10-13/2026-10-13/trial-interrupted-001/`.
`trial-interrupted-001.log` = host session log.

**Hypothesis**: FreeInk-style interruption of the native OTP waveform ~340 ms
after BUSY assert, then reset/reinit, yields readable black/yellow mono on the
chart and on real diagnostics text.

**Variables**: mode `I` (chart) and `d` with diagnostics routed through
`waveform=mono-interrupted` (temporary canvas path). Fixed: `cutoff_ms=340`,
`anchor=observed-BUSY-assert`, `polarity=white1-black0`, booster
`06 6F 1F 17 27`.

**Setup**: MAC `44:1b:f6:c1:31:90`, port then `/dev/cu.usbmodem20111401`.
Build string `Oct 13 2026 23:04:05`. External power (`0x05`), batt
4148→4156 mV, env 31.6 °C / 67 %.

**Sequence**: `p` → `I` → photo → `r` → photo → `d` → photo → `r` → photo →
`p`. One connection; final status received (uptime 41314 ms).

| Run | frame_ms | on | cutoff | total_ms |
| --- | ---: | ---: | ---: | ---: |
| I chart | 401 | 132 | 340 | 1393 |
| d diagnostics (mono-canvas) | 825 | 132 | 340 | 1817 (render total 2279 incl. 462 draw) |

Native restores: push 16905 ms, total 17373 ms each.

**Photos**:
- `00-I.jpg` — checker visible but whole field dark blue; squares dark vs
  medium blue, low contrast; faint red marks at right edge.
- `01-d.jpg` — dark blue field; diagnostics text barely discernible; ghost
  blocks of the previous dashboard (tag positions, red STALE area) remain.
- `00-restored.jpg`, `01-restored.jpg`, `before.jpg` — dashboard readable.

**Verdict (production README findings)**: table "Interrupted native | 1.39 s |
Dark checker, strong previous-image residue"; real diagnostics text
"interrupted native 2.28 s left almost unreadable text on a dark field".
Rejected. Also bounded USB regression evidence: both Trials, native
restorations and final status completed on one connection (uptime 41 s).
