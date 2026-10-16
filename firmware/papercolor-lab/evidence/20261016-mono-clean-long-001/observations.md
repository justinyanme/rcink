# 20261016-mono-clean-long-001 — own clean mono, 60 updates

**Variables**: `own_mono.py` waveform; text, white (01), black (02), 30 × (text, invert), white (03),
black (04), text (05), native cleanup (06).

**Result** (`tools/ghost_metric.py`): white Lab (68,−9,6) → (68,−10,11), black (19,8,−27) →
(21,8,−29); ghost white 1.14 → 0.74, black 0.54 → 0.61. BUSY 3.17 s per update.

**Verdict**: no darkening over 60 updates (white L* 68 → 68); small yellow drift (b* +5, vs +24 for
the shake waveforms).
