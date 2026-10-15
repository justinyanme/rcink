# 20261015-mono-001 — TICKEY mono audit (series 3a, #21)

**Hypothesis**: baseline, all native knobs (trace of the cold drive in serial.log).

**Variables**: mono, setup=native psr=5F69 booster=native pack=4. precondition `show white native` unless noted; cold `show text`, photo, warm `show checker 40`, photo; restore cleanup.

**Timings**: cold clear 4933 / busy 244 / 6728 ms; warm busy 267 / 1568 ms.

**Photos** (01-trace-off.jpg, 02-show-checker-40-mono.jpg, 03-cleanup.jpg): Text and checker squeezed into the top half (whole image ~2x shorter), pinkish light, yellow strip and grey field below; warm checker shows the text underneath.

**Verdict**: Fail. Geometry wrong under native PSR; LUT is applied (timings differ from OTP) but data format mismatched.
