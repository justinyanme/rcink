# 20261015-mono-010 — TICKEY mono audit (series 3a, #21)

**Hypothesis**: 2 bpp with each row sent twice gives correct geometry.

**Variables**: pack=2x (vs 007), setup=tickey psr=0FA9 booster=none. precondition `show white native` unless noted; cold `show text`, photo, warm `show checker 40`, photo; restore cleanup.

**Timings**: clear 1927 / busy 106 / 3126; warm 129 / 974.

**Photos** (01-show-text-mono-setuptickey-psr0FA9-boosternone-pack2x.jpg, 02-show-checker-40-mono-setuptickey-psr0FA9-boosternone-pack2x.jpg, 03-cleanup.jpg): Full-panel text, correct geometry, steel-blue on yellow, all sizes readable; warm checker correct but strong ghost of the text.

**Verdict**: Geometry solved. mono LUT leaves ghosting on warm transitions.
