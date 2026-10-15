# 20261016-color-own-001 — own six-colour waveform, two 2 bpp passes (first cut)

**Hypothesis**: own AC trains give six distinguishable colours full screen, far faster than native (14.4 s BUSY).

**Variables**: `tools/own_color.py` at this Trial: pass 1 (`map=30233133`) white = `2x30,0x10` + white tail,
blue `8x[1:10,2:6]`, yellow `4x[1:10,9:4]`, black `8x[8:6,2:10]`; pass 2
(`map=30020010`) green `8x[1:10,2:1]`, red `8x[2:10,9:5]`, black again; other pixels idle. `setup=tickey
psr=0FA9 booster=none win=1 pack=2`. Sequence: red, yellow, blue, green, white, black fills, chart, text, chart.

**Result** (camera ΔE76 to native inks): fills R 12, Y 12, B 9, G 17, W 10, K 11 (centre). Chart 07:
K 11 W 6 Y 11 R 12 B 15 G 17. Timing per image: pass 1 BUSY 1574 ms / total 2281, pass 2 1539 / 2246 →
**BUSY 3.1 s, total ~4.6 s** (native 14.4 / 16.0 s). 08: text crisp, white background keeps colour ghosts
of the previous chart. 09: text ghost in yellow and blue blocks.

**Verdict**: six colours distinguishable at 3.4x native speed; history ghosts in white/yellow/blue →
fixed for white/yellow in -002.
