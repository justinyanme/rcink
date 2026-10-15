# 20261015-hostscript-001 — lab.py acceptance (issue #18)

**Hypothesis**: `lab.py trial` runs a whole Trial on one console open: banner
wait, one result per step, rceye photo per `photo` step, final cleanup + photo.

**Variables**: none (native only). Build `c98d76f90c65` (clean), flashed via
`just flash-papercolor-lab` (= `lab.py flash`), app-only, MAC verified.

**Command**: `lab.py trial .cache/papercolor-lab/hostscript-001 --port
/dev/cu.usbmodem83301 --camera android:3B161600Z2U00000 --steps "info,show black
native,photo,cleanup,info"`; curated with `lab.py curate`.

| Step | Reply |
| --- | --- |
| boot | `# ready build=c98d76f90c65 … rev=01010400 supply=0x05 uptime_ms=246` |
| info | `temp=2100 uptime_ms=318` |
| show black native | `OK busy_ms=14389 total_ms=16137` |
| cleanup | `OK mode=native busy_ms=14389 total_ms=16130` |
| cleanup (restore) | `OK mode=native busy_ms=14389 total_ms=16130` |

**Verdict: pass.** One open, banner 317 ms after open, no retries, events
`open, boot, write/result ×5, photo ×2`. `01-show-black-native.jpg` and
`02-cleanup.jpg` (after restore) both show the whole panel uniformly black.
`busy_ms` 14389 identical to the skeleton Trial: the OTP waveform length is
fixed, independent of image. Originals: `.cache/papercolor-lab/hostscript-001/`.
