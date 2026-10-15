# 20261015-interrupt-001 — text, interrupt cut=250 (series 2, #20)

**Hypothesis**: an earlier cut leaves the background further from white (FreeInk: colours settle in order, white last).

**Variables**: cut 250 ms (vs 340 in -002). cdi 0F. Build `edd95fbd3537` (flash-020). Precondition every Trial: `show white native` (BUSY ~14387 ms), then one interrupted drive, photo; restore `cleanup` + photo.

**Reply**: `OK image=text mode=interrupt cut_ms=250 cdi=0f busy_ms=250 total_ms=685`.

**Photo** `01-show-text-interrupt-cut250.jpg`: background yellow-green, text dark navy; 13/16/18/24 pt all readable;
white-on-black line readable (navy band). No residue of the white field. Restore cleanup normal.

**Verdict**: readable; contrast lower than 340 (tinted background). Meets ≤2 s and 16 px readable.
