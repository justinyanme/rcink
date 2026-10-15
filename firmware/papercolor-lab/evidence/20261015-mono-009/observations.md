# 20261015-mono-009 — TICKEY mono audit (series 3a, #21)

**Hypothesis**: PSR second byte A9 alone changes contrast.

**Variables**: psr=5FA9 (vs 001). precondition `show white native` unless noted; cold `show text`, photo, warm `show checker 40`, photo; restore cleanup.

**Timings**: clear 4966 / busy 275 / 6789; warm 298 / 1598.

**Photos** (01-show-text-mono-psr5FA9.jpg, 02-show-checker-40-mono-psr5FA9.jpg, 03-cleanup.jpg): Geometry as 001 (squeezed top half) but dark navy on yellow, high contrast.

**Verdict**: PSR byte 2 (69 -> A9) affects drive contrast/polarity, not geometry.
