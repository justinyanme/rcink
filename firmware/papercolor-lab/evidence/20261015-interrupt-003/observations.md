# 20261015-interrupt-003 — text, interrupt cut=450 (series 2, #20)

**Hypothesis**: a later cut moves the background toward white.

**Variables**: cut 450 ms. cdi 0F. Build `edd95fbd3537` (flash-020). Precondition every Trial: `show white native` (BUSY ~14387 ms), then one interrupted drive, photo; restore `cleanup` + photo.

**Reply**: `OK image=text mode=interrupt cut_ms=450 cdi=0f busy_ms=450 total_ms=885`.

**Photo**: background pale blue-white (blue tint, not yellow), text navy; all sizes readable.

**Verdict**: readable; background hue moves yellow-green (250) → pale yellow (340) → pale blue (450): the cut
selects a waveform phase. 340–450 bracket the nearest-to-white background.
