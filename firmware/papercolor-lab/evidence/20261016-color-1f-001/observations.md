# 20261016-color-1f-001 — own six colours in ONE full-screen 4 bpp pass, ghost check

**Variables**: `tools/own_color.py` (PSR 1F A9, 4 bpp, every used row active 368 frames: clean
cycle `2x85` + tail, colour train; white = `20x[2:2, t1 2:2]` erase + cycles without yellow flick).
Sequence: chart, text, chart, white, red, yellow, blue, green, black fills.

**Result**: BUSY 4357 ms, total 5.27 s per image (native 14.4 / 16 s). Chart ΔE76 to native inks
(dim afternoon light): K 3, W 12, Y 15, R 13, B 8, G 11; min pairwise ΔE of the six **29** (native 31).
Fills R 12, Y 13, B 10, G 16, K 10. 02 text: white background uniform, no chart blocks; 03 chart
after text: no text visible in any block; 04 white after chart: uniform.

**Verdict**: six distinguishable colours, ~3× faster than native, no ghost of the previous image.
