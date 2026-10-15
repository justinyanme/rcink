# 20261016-long-002 — 120 balanced fast-mono updates (own waveform)

**Hypothesis**: a charge-balanced own waveform (to-white 85 white frames incl. tail, to-black 85
black frames, unchanged idle, rows right-aligned) does not drift over 120 updates.

**Variables**: 120 alternating `show black|white custom`, photos after updates 1, 2, 39, 40, 79,
80, 119, 120, then own clear + photo.

**Result** (centre L): black 60, 62, 61, 62; white 162, 166, 166, 167, 167 (after clear);
BUSY 1105–1210 ms per update. Earlier unaligned run (long-001: idle after black) gave black 73–85.

**Verdict: pass** — no drift; trailing idle after black must be avoided (right-align rows).
