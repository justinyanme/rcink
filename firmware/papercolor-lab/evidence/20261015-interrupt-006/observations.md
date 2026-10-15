# 20261015-interrupt-006 — residue loop, 10 × invert on checker 40 (series 2, #20)

**Hypothesis**: repeated interrupted updates accumulate residue; one Native refresh (Cleanup) removes it.

**Variables**: number of interrupted updates (1, 5, 10). cut 340, cdi 0F. Build `edd95fbd3537` (flash-020). Precondition every Trial: `show white native` (BUSY ~14387 ms), then one interrupted drive, photo; restore `cleanup` + photo.
Sequence: `show white native, show checker 40 interrupt`, 10 × `show invert interrupt` (photos after 1st,
5th, 10th), `cleanup`, photo (`--no-restore`).

**Replies**: every interrupt `busy_ms=340 total_ms=772..777`; cleanup `busy_ms=14388 total_ms=15929`.

**Photos**: 01 (after 1): light squares yellow, dark squares mid blue. 02 (after 5) and 03 (after 10): light
squares stay yellow, dark squares darken to navy; contrast grows, no ghost of the opposite phase, no seam.
04 (cleanup): clean black/white checker, no residue visible.

**Verdict: pass** (10 updates then Cleanup fully recovers). Light stays yellow, not white, after repeated
updates (FreeInk: "black on yellow").
