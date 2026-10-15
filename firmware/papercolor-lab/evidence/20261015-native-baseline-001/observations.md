# 20261015-native-baseline-001 — chart, Native refresh (series 1, issue #19)

**Hypothesis**: the Lab raw driver paints the 10-13 chart with the vendor OTP
waveform identically to the production M5GFX path (`20261013-prior-trial-native-001`).

**Variables**: image `chart`; mode `native`. Build `211f3650f5be`, flash-019.

**Sequence**: `info, show chart native, photo, info`, restore `cleanup` + photo.

| Step | Reply |
| --- | --- |
| info | `temp=1e00` |
| show chart native | `OK busy_ms=14506 total_ms=16333` |
| cleanup (restore) | `OK busy_ms=14509 total_ms=16250` |

**Photo** `01-show-chart-native.jpg`: layout matches the 10-13 chart: black
and white top patches; yellow, red, blue, green patches all clearly
distinguishable and saturated; ramp rows with black→green→yellow on the yellow
ramp, black→red, black→blue, black→green; 20 px footer checker crisp. 4 px
border visible. `02-cleanup.jpg` identical.

**Verdict: pass, reference.** Six colours distinguishable. `busy_ms` 14506 is
117 ms longer than the text/field drives (14387–14389) of the following Trials;
`temp` was the lowest of the session (`1e00` vs `1f80`/`2000`), so BUSY length
may follow temperature rather than image (not separated here).
