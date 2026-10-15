# 20261015-interrupt-005 — text, interrupt cdi=3F (series 2, #20, extra)

**Hypothesis**: FreeInk's CDI `0F` (vs native `3F`, bits 5:4) matters for the interrupted drive.

**Variables**: cdi 3F (vs 0F in -002). cut 340. Build `edd95fbd3537` (flash-020). Precondition every Trial: `show white native` (BUSY ~14387 ms), then one interrupted drive, photo; restore `cleanup` + photo.

**Reply**: `OK image=text mode=interrupt cut_ms=340 cdi=3f busy_ms=340 total_ms=775`.

**Photo**: polarity inverted: whole field navy blue with pale text; the white-on-black line becomes navy text
on pale. Restore normal.

**Verdict: confirmed** CDI bits 5:4 act as data polarity for this drive; `0F` is required for dark-on-light.
