# 20261015-mono-007 — TICKEY mono audit (series 3a, #21)

**Hypothesis**: 2-bit packing fixes the stripes.

**Variables**: pack=2 (vs 005). precondition `show white native` unless noted; cold `show text`, photo, warm `show checker 40`, photo; restore cleanup.

**Timings**: clear 1927 / busy 106 / 3002; warm 129 / 850.

**Photos** (01-show-text-mono-setuptickey-psr0FA9-boosternone-pack2.jpg, 02-show-checker-40-mono-setuptickey-psr0FA9-boosternone-pack2.jpg, 03-cleanup.jpg): Horizontal scale and stripes fixed; navy on yellow; but the whole image fills only the top ~half: 40 px squares appear 40x20.

**Verdict**: Packing confirmed (2 bits per pixel); rows: controller consumes two data rows per panel row.
