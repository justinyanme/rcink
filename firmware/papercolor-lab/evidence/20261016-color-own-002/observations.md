# 20261016-color-own-002 — own six-colour waveform, ghost check chart → text → chart

**Hypothesis**: a short white/black shake (`4x[8:6,2:10]`) before white and yellow, and a 12-cycle blue,
remove the ghosts seen in -001.

**Variables**: `tools/own_color.py` as committed (SHAKE before white/yellow, blue `12x[1:10,2:6]`).
Sequence: chart, text, chart.

**Result**: chart ΔE76 K 11 W 9–10 Y 6–7 R 11–12 B 6 G 15; minimum pairwise ΔE between the six > 20.
02 text: crisp; white background faintly tinted where the chart's red/yellow blocks were. 03: yellow
block clean; blue block shows the text as slightly lighter blue (≈ΔE 3–5 on bands). Pass 1 BUSY 2291 /
total 3078 ms, pass 2 1539 / 2326 → **BUSY 3.8 s, total ~5.4 s** (native 14.4 / 16.0 s, 2.9x).

**Verdict**: six-colour goal met (distinguishable, ~3x faster). Residual: faint blue ghost and white tint
after a strongly different previous image. Longer shakes (8 cycles) made white yellower and did not
remove the blue ghost; a DC-balanced black (`8x[8:8,2:8]`) did not either.
