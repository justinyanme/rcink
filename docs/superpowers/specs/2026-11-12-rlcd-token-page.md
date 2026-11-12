## Problem Statement

The RLCD's Allowance page shows how much weekly subscription Allowance each Provider has left, but not how many tokens the owner actually burns. Daily Token usage is already collected from the owner's Macs and stored per Provider, per Source and per day in Just Ship's D1. The only read path, the public `GET /api/token-usage`, merges every Provider into one daily total for the website heatmap.

Two gaps keep that data off the device:

- **Freshness.** The M4 is the main Source. Its collector runs once a day at 03:00 with a completion gate, so today's Token usage stays nearly empty until the next morning. The MBP16 already uploads hourly.
- **Read path.** No endpoint returns Token usage per Provider.

The board has two firmware buttons left. PWR is broken, and it is a hardware power key anyway. BOOT syncs now, and KEY steps between Pages.

## Solution

Add a **Token page** as the second Page of the Deck. KEY then cycles Allowance page → Token page → climate Dashboard → Allowance page.

**Rows and contents.** The Token page has three rows in Allowance-page order: Claude, Codex and Gemini, with the same names and logos. Kimi has no Token usage and is left out. Each row answers four questions:

- **A. Who used the most recently:** a large Seven-day total.
- **B. Today compared with normal:** today so far as a share of the Usual day, e.g. `today 85M · 60% of a usual day`.
- **C. The trend:** a 30-day bar chart scaled per row, with a dotted Usual-day line. Today's bar is hollow because the day is incomplete.
- **D. The running total:** a Lifetime odometer, `ODO 41.2B`, counted since the first record (2025-09-18).

**Data path.**

- The M4 collector moves to the MBP16's hourly cadence.
- Just Ship gains a private, read-key-only endpoint that returns 30 days of per-Provider daily Token usage plus Lifetime.
- The RLCD fetches it alongside the Allowance snapshot, persists the last good copy, and shows it truthfully when stale or offline.

**Mock** (400×300, 1-bit):

```text
┌──────────────────────────────────────────────────┐
│ ✦ Busiest this week: Claude                      │
├──────────────────────────────────────────────────┤
│ (C) Claude             ▁▂▅▃▇▂▁▃▅▆▂▃▇█▅▃▂▄▅▆▃▂▅▇▆▄▃▅▆▯│
│     3.9B  7 days      ┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈│
│     today 85M · 60% of a usual day   ODO [4][1].[2]B│
├──────────────────────────────────────────────────┤
│ (X) Codex   …                                    │
├──────────────────────────────────────────────────┤
│ (G) Gemini  …                                    │
├──────────────────────────────────────────────────┤
│ Synced 16:12                       since Sep '25 │
└──────────────────────────────────────────────────┘
```

## User Stories

1. As the owner, I want a Token page next to the Allowance page, so that I can see consumption as well as what is left.
2. As the owner, I want KEY to cycle Allowance page → Token page → climate Dashboard, so that one button reaches every view.
3. As the owner, I want BOOT to sync the Page I am looking at, so that I can refresh the Token page without leaving it.
4. As the owner, I want BOOT on the climate Dashboard to go to the Allowance page and sync, so that its behavior there is unchanged.
5. As the owner, I want one row each for Claude, Codex and Gemini, named and drawn as on the Allowance page, so that a Provider is recognizable across Pages.
6. As the owner, I want each row's Seven-day total to be the most prominent number, so that I can see at a glance who I used most recently.
7. As the owner, I want today's Token usage shown as a share of a Usual day, so that I know whether today is heavy without a misleading full-day projection.
8. As the owner, I want a 30-day bar chart per row with the Usual day marked, so that I can see the trend and today's position in it.
9. As the owner, I want a Lifetime odometer per row that only grows, so that the total feels like a car's mileage.
10. As the owner, I want my Macs' Token usage merged, so that the page shows my total rather than per-machine numbers.
11. As the owner, I want today's numbers to include the M4 within about an hour, so that "today" is real during the day.
12. As the owner, I want the headline to call out a Provider that is genuinely on a tear today, or else name the busiest Provider this week, so that the page has a point of view.
13. As the owner, I want a Provider with tiny usage not to trigger "on a tear" from a small base, so that the headline reflects real volume.
14. As the owner, I want the footer to warn when the M4 has not uploaded for over two hours, so that a silent collector cannot pass for a quiet day.
15. As the owner, I want a failed fetch to keep the last good numbers and say `SYNC FAILED · retry HH:MM`, so that an outage looks like an outage.
16. As the owner, I want the last good Token page restored after a restart or deep sleep, so that the device is useful before its next fetch.
17. As the owner, I want a short entry animation in HOST (bars grow, odometer digits roll) and a still page otherwise, so that it is fun without hurting legibility.
18. As the site owner, I want per-Provider data to stay private behind the device's existing read key, so that the public heatmap contract and privacy boundary are unchanged.

## Implementation Decisions

- **Repository ownership:**
  - Just Ship owns the collector cadence, the new Worker read endpoint and D1 queries.
  - rcink owns the RLCD fetch, persistence, view model, rendering and navigation.
  - The public `GET /api/token-usage` and its edge cache are unchanged.
- **M4 cadence:**
  - Match the MBP16: run at minute 7 of every hour, `TOKEN_USAGE_COMPLETION_GATE=0`, `TOKEN_USAGE_DEFAULT_LOOKBACK_DAYS=2`, providers `codex,claude,agy`.
  - Update the canonical launchd definition in Just Ship if one exists, then the installed M4 job.
  - Measure one M4 run's duration before enabling it hourly; ccusage parses local logs offline.
- **Read endpoint:**
  - Add an authenticated `GET` path to the Worker's route allow-list that requires `X-Usage-Read-Key`.
  - The write key is rejected, there is no query or body, and responses use `Cache-Control: no-store`.
  - Reuse the existing request-boundary, JSON-response and rate-limit conventions.
- **Response contract:** schema `rcink.token-usage.v1`, with these fields:
  - `servedAt`: epoch seconds.
  - `today`: the Asia/Shanghai date the series ends on.
  - `requiredSourceCollectedAt`: the newest `collected_at` from `mac-mini`, or null.
  - `providers`: exactly three entries in the order `claude`, `codex`, `gemini`. Each entry has `days`, which holds 30 non-negative integers from oldest to today with missing days zero-filled, and `lifetime`, the sum over every stored row with no 365-day cap.
  - Provider mapping: D1 `claude-code`→`claude`, `codex`→`codex`, `antigravity`→`gemini`.
  - Sources are summed. No account, Source or raw data is exposed.
- **Day boundary:**
  - The Worker's `today` defines the series.
  - If the device's local date (UTC+8) passes it before the next fetch, the view shifts the series by the elapsed days and counts the new today as zero so far, rather than relabeling yesterday as today.
- **Derived values** (device view model, from `days`):
  - Seven-day total: the last 7 entries.
  - Usual day: the mean of the 7 entries before today.
  - Today's share: today ÷ Usual day.
  - Without a nonzero Usual day, show only `today N` and never "on a tear".
- **Headline,** first match wins:
  1. `<Provider> is on a tear today`: today ≥ 150% of its Usual day and ≥ 25% of the three Providers' combined Usual day.
  2. `Busiest this week: <Provider>`: largest Seven-day total.
  3. `Waiting for the first numbers.`
  4. The stale and failed states below suppress rule 1.
- **Footer:**
  - Normally `Synced HH:MM`.
  - `OLD DATA · M4 HH:MM` when `requiredSourceCollectedAt` is null or more than 2 h old. The numbers stay.
  - `SYNC FAILED · retry HH:MM` after a failed Token fetch.
  - A quiet MBP16 is normal and is not flagged.
- **Number format:** at most three significant digits with K/M/B suffixes, e.g. 940K, 85M, 850M, 3.9B, 41.2B.
- **Layout:**
  - A headline with a rule, three ~84 px rows and a footer, drawn with the existing renderer, fonts, logos and light polarity.
  - Bars are scaled per row. Today's bar is hollow. The Usual day is a dotted line.
- **Device fetch:**
  - A Token fetch rides in the same refresh worker run and Wi-Fi session as an Allowance fetch, as a second GET.
  - HOST: when at least 15 min have passed since the last Token success.
  - Battery modes: at most hourly.
  - BOOT: always fetches both, and the footer shows `SYNCING` while it runs.
  - Token and Allowance failures are tracked and displayed independently.
- **Persistence:** store a separate bounded, versioned NVS blob holding the last complete, validated Token response. Restore it through the same decoder, and write it only when it changes, following the Allowance blob's rules.
- **Navigation:** the Deck holds two Pages, Allowance (0) and Token (1); the existing next-press ring already yields the agreed order. BOOT on any Deck Page syncs in place, and on the Dashboard it opens the Allowance page and syncs.
- **Animation:** HOST only. On sync or entering the Page, a 0.9 s sweep grows bars left to right and rolls odometer digits up. It is otherwise static, so no idle redraws. ECO and ACTIVE draw the static frame retained through deep sleep.
- **Cost:** negligible.
  - Device: about 96 reads/day in HOST, each scanning about 3k D1 rows for Lifetime, against Workers Paid's included row reads.
  - M4: 24 runs/day × 6 rows of upserts.

## Testing Decisions

- Prefer externally observable results: API responses, decoded view state and decoded framebuffers. Avoid private helpers.
- **Worker seam** (Vitest + D1, existing setup):
  - Read-key auth; write-key and missing-key rejection; exact schema and order.
  - Zero-filled gaps; summing across Sources.
  - Lifetime including rows older than 365 days.
  - The `mac-mini` collected-at, including when the M4 never uploaded.
  - `no-store`; the public endpoint unchanged.
- **Collector:** confirm the hourly M4 run uploads today's rows for all three Providers, with no historical rewrite beyond the 2-day lookback.
- **Device native tests:**
  - Decoder bounds and malformed responses.
  - Seven-day total, Usual day and today's share, including fewer than 7 complete days and a zero Usual day.
  - Headline rules, including the 25% materiality threshold.
  - OLD DATA and SYNC FAILED.
  - The midnight series shift.
  - Number formatting.
  - The persistence round trip.
  - The navigation ring and BOOT in place.
- **Hardware acceptance:**
  - Compare the authenticated GET with serial `layout_json` and a decoded framebuffer for all three rows.
  - Check that nothing clips, the KEY ring works, BOOT syncs in place, and a forced fetch failure shows SYNC FAILED.

## Out of Scope

- Kimi Token usage, the input/output/cache split, API-price cost, and per-Source display.
- Changing the public heatmap endpoint or its cache, or making per-Provider data public.
- The ideas parked in https://github.com/justinyanme/rcink/issues/13: year heatmap, milestone celebrations, API-price value, BOOT long-press, use-it-or-lose-it nudge.
- Idle animation on the Token page, other firmware targets, and DSL changes.

## Further Notes

- Facts checked on 2026-11-12:
  - **D1:** `token_usage_daily` keeps only `total_tokens` per (day, provider, source).
  - **MBP16:** collects `codex,claude` hourly at :07 with lookback 2 and the gate off.
  - **M4:** collects `codex,claude,agy` daily at 03:00 with the gate on.
  - **History:** data starts 2025-09-18. Daily totals run 250M–1.6B, and the last 365 days sum to about 69.3B.
- `agy`/`antigravity` runs ccusage's `gemini` reader, which reads Gemini CLI logs (`~/.gemini/tmp`). The Allowance page's Gemini is Antigravity's weekly pool. Both are shown as the Provider "Gemini".
- On the RLCD's Wi-Fi a TLS handshake takes 5–10 s, so the second GET adds that much to a refresh. It runs off the UI task, and the HOST failure retry is 60 s.
