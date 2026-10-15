# 20261013-prior-final-native-001 — final native acceptance

**Prior baseline, production firmware port, PaperColor booster.** Driven by
`firmware/papercolor` final application `flash-015` (hash-verified app-only
write) via `just trial-papercolor … Nd`, not Lab firmware. Archive original:
`.cache/papercolor/rmbp-2026-10-13/2026-10-13/final-native-001/`.
`final-native-001.log` = host session log.

**Hypothesis**: after removing the experimental diagnostics path, native
refresh still draws chart, dashboard and diagnostics readably, with complete
console acknowledgements on one connection.

**Variables**: none beyond the Trial sequence; native OTP waveform throughout.
Booster `06 6F 1F 17 27` logged on the chart drive.

**Setup**: MAC `44:1b:f6:c1:31:90`, port then `/dev/cu.usbmodem20111401`.
Build string `Oct 13 2026 23:16:49`. External power (`0x05`), batt 4166 mV,
env 31.5 °C / 67 %.

**Sequence**: `p` → `N` → photo → `r` → photo → `d` → photo → `r` → photo →
`p`. Uptime 970 → 70636 ms, no reboot.

| Step | ms |
| --- | ---: |
| N frame / on / native-refresh / off | 490 / 132 / 14512 / 173 |
| N total_ms | 16008 |
| `r` dashboard push / total | 16905 / 17373 |
| `d` diagnostics push / total | 16903 / 17365 |

**Photos**:
- `00-N.jpg` — chart same as `trial-native-001`: distinct six-colour blocks,
  crisp black/white checker.
- `01-d.jpg` — native diagnostics page readable (HEAP, BATT 97 %, RSSI
  offline, WAKE 1, RESULT boot).
- `00-restored.jpg`, `01-restored.jpg` — dashboard readable with red/green/blue
  tags; final screen is the dashboard.

**Verdict (production README)**: "`final-native-001` passed native chart →
dashboard → native diagnostics → dashboard with settled JPEG inspection,
complete newline-terminated command acknowledgements and external-power uptime
970→70636 ms. Diagnostics 17.365 s; dashboard 17.373 s." Pass.
