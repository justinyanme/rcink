# 20261015-native-baseline-002 — text, Native refresh (series 1, issue #19)

**Hypothesis**: product-font text (VT323 13/16/18/24 pt, black on white, one
16 pt white-on-black line) is readable at every size under the Native refresh;
this is the readability reference for Fast mono.

**Variables**: image `text`; mode `native`. Build `211f3650f5be`, flash-019.

**Sequence**: `info, show text native, photo, info`, restore `cleanup` + photo.

| Step | Reply |
| --- | --- |
| info | `temp=1f80` |
| show text native | `OK busy_ms=14387 total_ms=16142` |
| cleanup (restore) | `OK busy_ms=14387 total_ms=16128` |

**Photo** `01-show-text-native.jpg`: all four sizes fully legible, including
13 pt lowercase (`mnopqrstuvwxyz`) and digits; strokes solid black, background
light grey-white, no ghosting. White-on-black 16 pt line legible. Same in
`02-cleanup.jpg`.

**Verdict: pass, reference.** 16 px text readable.
