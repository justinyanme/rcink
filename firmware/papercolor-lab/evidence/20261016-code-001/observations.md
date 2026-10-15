# 20261016-code-001 — phase-0 drive code survey (own waveforms)

**Hypothesis**: the 4-bit t0 code in raw level bits 5:2 selects a fixed drive; 80 frames from native
white and native black reveal what each code does.

**Variables**: code 0..15, start white (photos 01-16) / black (17-32); custom mode, all rows same,
VCOM idle, `setup=tickey psr=0FA9 win=1 pack=2`, 80 frames (975 ms BUSY each).

**Result** (centre crop L, from white / from black): `0` 175/58 none; `1` 173 (yellow) / 165 yellow;
`2` 58 black / 50; `3` 171/55 ~none; `4` none; `5` = `1`; `6` = `2`; `7` = `3`; `8` 175 / 144 white;
`9` = `8`; `10` 172/55 cancel; `11`,`14`,`15` none; `12`,`13` = `8`.
`sheet.jpg` = all 32 photos (panel crop).

**Verdict**: bits 3:2 → white, bit 1 → black, bit 0 → yellow; white+black cancel.
