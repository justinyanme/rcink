# 20261015-skeleton-001 — raw driver acceptance (issue #17)

**Hypothesis**: this repo's own SPI driver (no M5GFX), with the pinned
Panel_ED2208 bring-up copied byte for byte, drives a full native field.

**Variables**: none. Native OTP waveform, pinned init, pinned booster.

**Setup**: PaperColor `44:1b:f6:c1:31:90` on `/dev/cu.usbmodem83301`, external
USB power (PM1 supply `0x05`). Camera OPPO PLJ110 via rceye
`android:3B161600Z2U00000`, stills. Build `9e7f968da28e-dirty` (lab tree not
yet committed when flashed); app-only flash `012a7e57…39c66`, 284064 bytes,
hash-verified against the live partition table.

**Sequence** (`events.jsonl`): `info` → `show black native` → photo →
`show white native` → photo → `info`. One USB connection, no retries.

| Step | Reply |
| --- | --- |
| boot | `# ready … rev=01010400 supply=0x05 uptime_ms=246` |
| show black native | `OK busy_ms=14389 total_ms=16137` |
| show white native | `OK busy_ms=14389 total_ms=16137` |
| info (after) | `temp=2100` (was `2080` cold), uptime 40671 ms |

**Verdict: pass.** `01-black.jpg` shows the whole active area uniformly black,
`02-white.jpg` uniformly white, no banding or residue. Register 0x70 reads
non-zero (`01 01 04 00`). BUSY-asserted time is identical for both fields and
sits ~1.6 s under the 16.0 s native reference measured on 2026-10-13 inside the
production firmware (that figure included reset/init and frame generation).

Originals (3072² JPEG) stay in `.cache/papercolor-lab/accept-001/`.
