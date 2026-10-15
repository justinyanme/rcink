# 20261015-mono-008 — TICKEY mono audit (series 3a, #21)

**Hypothesis**: PSR first byte 0F alone causes the format change.

**Variables**: psr=0F69 (vs 001). precondition `show white native` unless noted; cold `show text`, photo, warm `show checker 40`, photo; restore cleanup.

**Timings**: clear 4936 / busy 244 / 6728; warm 267 / 1567.

**Photos** (01-show-text-mono-psr0F69.jpg, 02-show-checker-40-mono-psr0F69.jpg, 03-cleanup.jpg): 2x stretched + striped like 003, dark grey-navy.

**Verdict**: Confirmed: PSR byte 1 (5F -> 0F) switches the frame format.
