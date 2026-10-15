# 20261015-color-003 — TICKEY staged colour audit (series 3b, #22)

**Hypothesis**: PSR 5F keeps 4 bpp; line doubling restores geometry.

**Variables**: fast, setup=tickey psr=5FA9 booster=none pack=4x. Precondition `show white native`; one drive; photo; restore cleanup + photo.

**Timings (ms)**: clear 1925, p1 893, final 106, busy 999, total 5672.

**Photo**: Correct scale but wrapped: footer checker at top, source rows 0-99 missing, stale band at bottom: the 240000 B frame overflows at ~200000 B.

**Colour patches**: 4 of 6 separable: yellow (yellow), red (dark yellow), blue (light blue), green (olive); black dark grey; white not visible (wrapped away). **White field**: background light grey-yellow.

**Verdict**: Fail; best colour so far. Cleanup (`02-cleanup.jpg`) restores the native chart.
