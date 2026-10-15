# 20261015-mono-003 — TICKEY mono audit (series 3a, #21)

**Hypothesis**: psr=0FA9 alone changes the result.

**Variables**: psr=0FA9 (vs 001). precondition `show white native` unless noted; cold `show text`, photo, warm `show checker 40`, photo; restore cleanup.

**Timings**: clear 4966 / busy 275 / 6789; warm 298 / 1599.

**Photos** (01-show-text-mono-psr0FA9.jpg, 02-show-checker-40-mono-psr0FA9.jpg, 03-cleanup.jpg): Image stretched 2x horizontally (right half off-panel), every other column striped; vertical scale normal-looking; navy on cream.

**Verdict**: PSR changes the data format: 4-bit codes read as two 2-bit pixels (00 + code) -> stripes.
