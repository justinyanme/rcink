# 20261015-interrupt-004 — checker 40, interrupt cut=340 (series 2, #20)

**Hypothesis**: large areas (40 px squares) drive as uniformly as text strokes.

**Variables**: image checker 40 (vs text). cut 340, cdi 0F. Build `edd95fbd3537` (flash-020). Precondition every Trial: `show white native` (BUSY ~14387 ms), then one interrupted drive, photo; restore `cleanup` + photo.

**Reply**: `OK image=checker size=40 mode=interrupt cut_ms=340 cdi=0f busy_ms=340 total_ms=772`.

**Photo** `01-show-checker-40-interrupt.jpg`: dark squares blue (not black), light squares pale cream; edges
crisp; no gate-scan seam band visible. `02-cleanup.jpg`: black/white checker normal.

**Verdict**: uniform; dark = blue at this cut (black needs the full waveform).
