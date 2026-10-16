# 20261016-grid-1f-001 — own 48-colour rendering: 8 bases x 8 modifiers, two full-screen 4 bpp passes

**Variables**: `tools/own_grid.py` (image `grid`, 8×8 patches of 50×75 px, value 0..63). Pass 1
(`psr=1FA9 pack=4 field=0:3`): bases white, black, red, blue, yellow, green, pink (red + `8x4`),
orange (red + `1x3`). Pass 2 (`field=3:3`): none, `2x1`, `2x1,8x1`, `8x6`, `1x1`, half red cycle
`[2:5,9:3]`, half green cycle `[1:5,2:1]`, t1 code 2 ×3. Modifiers/bases chosen by hill climbs
(scratch `search48e..h`) and a base-6 trial (teal → pink: 47.75 → 51 mean).

**Result** (largest set pairwise ≥ ΔE76, randomised greedy over 64 patches; four independent runs,
`patches-run1..4.json`; white-normalised = channels scaled so patch 0 equals the daylight native
white 164,175,176, removing the evening room-light level):

| run | raw ≥5 / ≥8 | white-normalised ≥5 / ≥8 |
| --- | --- | --- |
| 1 | 46 / 28 | 49 / 35 |
| 2 | 46 / 29 | 49 / 40 |
| 3 | 48 / 31 | 48 / 37 |
| 4 | 47 / 31 | 50 / 39 |

BUSY 2291 + 129 ms, total ~4.1 s. Reference: TICKEY's nominal 48-entry palette scores 40 at ΔE≥5
on the same greedy metric (nominal RGB).

**Verdict**: ≥ 48 colours pairwise distinguishable at ΔE 5 in every run (white-normalised; raw
46–48 under dim evening light).
