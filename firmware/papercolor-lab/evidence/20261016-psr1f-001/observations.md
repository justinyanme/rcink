# 20261016-psr1f-001 — PSR byte 0 bit-depth probe: 1F = 4 bpp over all 600 rows

**Variables**: white base (2 bpp), then image `bands` (8 bands, LUT rows 1-8 = W K Y R B G teal grey)
with PSR byte 0 `1F` / `4F` and 4 bpp (`map=01234567`) or 2 bpp (`map=01230123`) data.

**Result**: 01 `1F` + 4 bpp: all eight bands, band 7 reaches the bottom (rows 525-599) — unlike `5F`,
whose 4 bpp LUT frames stop at row ~491. 02 `1F` + 2 bpp: bands doubled at half height (data read as
4 bpp). 03 `4F` + 4 bpp: striped garbage. 04 `4F` + 2 bpp: 2 bpp bands, stops near row 490.
(`5F`→`57` only flips the content inside rows 0-491; `5B` garbage; DTM2 `13` plane is ignored.)

**Verdict**: `psr=1FA9` gives one full-screen pass with 8 LUT rows → `own_color.py` single pass.
