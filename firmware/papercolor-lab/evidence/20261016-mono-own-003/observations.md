# 20261016-mono-own-003 — own fast mono + own clear on text / checker

**Hypothesis**: own fast mono gives black-on-white text; ghosts from idle unchanged pixels are removed
by the own clear.

**Variables**: fast = to-white `8x60,1x5,8x20,2x1,8x5`, to-black `2x100`; clear = every pixel
`opposite, 0x30, target`. Sequence: native white, text, 10 × invert, checker 40, clear(checker),
fast(text), native cleanup.

**Result**: 01 black-on-white text, all sizes readable; 02 after 10 inverts whites slightly warm;
03 checker shows text ghost (idle pixels); 04 after own clear (2.6 s BUSY) no ghost; 05 text after
clear: faint checker ghost only; 06 native cleanup normal. Fast BUSY 1210 ms, total ~1.94 s.

**Verdict**: near black/white mono at 1.2 s; ghosting controlled by the own clear (2.6 s).
