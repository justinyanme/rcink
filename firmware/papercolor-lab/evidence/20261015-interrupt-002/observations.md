# 20261015-interrupt-002 — text, interrupt cut=340 (series 2, #20)

**Hypothesis**: FreeInk's default cut (340 ms after BUSY assert) on this raw driver gives readable mono text,
unlike the 10-13 port (`20261013-prior-trial-interrupted-001`, "almost unreadable").

**Variables**: cut 340 ms (FreeInk default), cdi 0F. Build `edd95fbd3537` (flash-020). Precondition every Trial: `show white native` (BUSY ~14387 ms), then one interrupted drive, photo; restore `cleanup` + photo.

**Reply**: `OK image=text mode=interrupt cut_ms=340 cdi=0f busy_ms=340 total_ms=775`.

**Photo**: background pale yellow-white, text navy; all sizes incl. 13 pt readable; white-on-navy line readable.
Restore cleanup normal.

**Verdict: pass vs Fast mono speed/readability** (0.78 s total). Best background of the three cuts together with 450.
Difference from the 10-13 port: `83` full window sent, no 200 ms power-on delays, CDI 0F only around the drive,
white field before (not a dashboard).
