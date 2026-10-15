# 20261015-mono-002 — TICKEY mono audit (series 3a, #21)

**Hypothesis**: setup=tickey alone changes the result.

**Variables**: setup=tickey (vs 001). precondition `show white native` unless noted; cold `show text`, photo, warm `show checker 40`, photo; restore cleanup.

**Timings**: clear 1915 / busy 94 / 3507; warm 117 / 1367.

**Photos** (01-show-text-mono-setuptickey.jpg, 02-show-checker-40-mono-setuptickey.jpg, 03-cleanup.jpg): Same squeezed geometry as 001, darker grey; strong text ghost under warm checker.

**Verdict**: Setup registers change timing (clear 4.9 -> 1.9 s, drive 244 -> 94 ms: frame rate), not geometry.
