# 20261016-mono-clean-001 — own clean mono, ghost check

**Variables**: `tools/own_mono.py`: every pixel, history ignored: white = tail + `2x85` + tail,
black = `2x85` + tail + `2x85` (tail `8x60,1x5,8x20,2x1,8x5`); `map=10111111`, 2 bpp.
Sequence: text, white, black, text, checker 40, white, native cleanup.

**Result** (`tools/ghost_metric.py`): white after text **hp 0.73**, after checker **0.69**
(native 0.60); black after white 0.47 (native 0.34). White Lab (67,−9,3..6) vs native (69,−5,−5);
black (20,8,−29) vs native (16,4,−6). BUSY 3170 ms, total 3.88 s (native 14.4 / 16 s).

**Verdict**: ghost at native level; near black/white (white slightly yellow-green, black bluish).
Rejected on the way (scratch Trials, 2026-10-16): fast white/black shake `4x[8:6,2:10]` → ghost
low but white yellows (b* +24 after 60 updates, also with DC balance and without yellow flick);
slow shake `2x[8:20,2:20]` → ghost 1.3× worse; shortened cycles (`8x40,2x50`) → ghost +30 %.
