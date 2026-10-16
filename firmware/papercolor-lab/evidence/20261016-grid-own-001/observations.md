# 20261016-grid-own-001 — own staged colour, 48-colour attempt (64 classes)

**Hypothesis**: three passes composing eight base trains with partial shifts give ≥ 48 distinguishable
colours.

**Variables**: `tools/own_grid.py` (image `grid`, 8×8 patches in the top 480 rows, value 0..63).
Pass 1 `pack=4 psr=5FA9 field=0:3`: bases white, black, red, blue, yellow, green, teal
`8x[1:10,2:2]`, dark yellow (yellow + `2x1`). Pass 2 `pack=2 psr=0FA9 field=3:2`: idle, `2x1`,
`2x1,8x1`, `1x1`. Pass 3 `field=5:1`: idle, `8x1`. Chosen by a 40-step hill climb (scratch
`search48b`, best it024) from 4 earlier designs (3 × 2 bpp passes: 34–43).

**Result**: 64 patches → greedy camera ΔE76 distinct: **45 at ≥5**, 30 at ≥8, 24 at ≥10, 14 at ≥15
(it024 in the search: 45/33/28/14; repeat noise ≈ ±3). Visible: white, greys, black, dark red, red,
pink, orange, olive, yellow, pale yellow, green, teal, sky and deep blue. BUSY 2289 + 59 + 47 ms,
total ~4.7 s. Rows below 480 are value 0 and partly undriven by the 4 bpp pass (ignore).

**Verdict**: 48 not reached at ΔE≥5 (45); one frame (11.7 ms) is the smallest step, and 1-frame
yellow or black pulses already move a base by ΔE 20–35, so many shifted patches merge. Reference:
TICKEY's nominal 48-entry quality palette counts 40 at ΔE≥5 on the same metric (nominal RGB, not
measured).
