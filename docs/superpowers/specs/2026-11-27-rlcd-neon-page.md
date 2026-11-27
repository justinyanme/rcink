# RLCD Neon Page — the cyberpunk-lofi clock Deck page

Mock (400×300, 1-bit, self-contained HTML with polarity / HOST-ECO / density / failure-state toggles):
[`assets/neon-page-demo.html`](assets/neon-page-demo.html)

## Problem Statement

The RLCD Deck's four Pages (Station, Allowance, Token, Monument) all answer questions about AI subscriptions: fuel left, tokens burned, distance covered. None of them is a *clock*. The owner keeps the instrument on the desk beside the Mac and glances at it all day; the question a desk instrument is asked most often — "what time is it" — is answered only by a small HH:MM tucked into the Station and Monument headers, legible only up close.

At the same time the Deck reads as four variations of the same table. It is precise, but there is no single Page the owner wants to leave pinned.

The owner wants a fifth Page whose subject is the time itself, drawn in the cyberpunk-lofi pixel-art register — a night alley in the rain, neon signs, a street stall — so the instrument is also a thing worth looking at. The Deck's important telemetry may appear only as parts of the scene, never as chrome competing with the clock.

## Solution

Add the **Neon Page** as the fifth Page of the Deck: a 400×300 1-bit pixel-art night alley whose hero is a rooftop billboard clock — HH:MM in ~35 px digits, readable at 80 cm — with the weekday, day-of-month and room temperature as the billboard caption.

Deck telemetry is woven into the scene as light:

- **Allowance → the FUEL sign** on the left building: one row per Provider, an initial and a bar for the remaining share, `!` plus a blink under 20 %, `···` when unavailable.
- **Token Skyline → the far skyline**: eight buildings whose heights are the eight weekly bars.
- **Total ODO → the LED ticker** on the stall front.
- **Battery → the battery neon** on the right building; it blinks when low.
- **Ambient Chrome → the billboard caption.**
- **Desk Cat → sitting at 2× on the stall counter**, beside a lofi worker at a laptop under two hanging lamps.

Truthfulness keeps the Deck's grammar and adds one scene-level rule, the **Neon Outage**: on a Link failure every data-bearing sign goes dark (dashed outline only) while the billboard (`--:--` when the clock is invalid), the moon and the lit stall stay on, so the state is legible from across the room without reading. Page-level Sync failure and Old data keep the signs lit with retained values and use the existing footer chips.

Both panel polarities are first-class: light polarity draws the scene as black ink on paper (linocut), dark polarity as white ink on night. HOST animates only four bounded things at 8 FPS (rain, cat tail, two tired-neon letters, reflection shimmer); ACTIVE and ECO draw a deterministic static frame.

## User Stories

1. As the owner, I want a Deck Page whose primary content is the current time, so that I can read the clock from arm's length without walking up to the desk.
2. As the owner, I want the clock driven by the RTC, so that the Page stays correct when Wi-Fi is down.
3. As the owner, I want the billboard to read `--:--` when the wall clock is invalid, so that the Page never shows a wrong time.
4. As the owner, I want the Page drawn as a cyberpunk-lofi night alley, so that the instrument is pleasant to leave on the desk all day.
5. As the owner, I want the time on a rooftop billboard as the hero of the scene, so that nothing competes with it.
6. As the owner, I want the weekday and day-of-month under the clock, so that the Page replaces a calendar glance.
7. As the owner, I want the room temperature in the billboard caption, so that Ambient Chrome is present on this Page as on every other.
8. As the owner, I want each Provider's remaining Allowance as a bar on the FUEL sign, so that I can see fuel left without leaving the clock Page.
9. As the owner, I want Providers in the Allowance Page's order with their initials, so that the rows mean the same thing as everywhere else.
10. As the owner, I want a Provider under the low-fuel threshold marked `!` with a blinking bar, so that low fuel catches my eye from across the room.
11. As the owner, I want a Provider with no data shown as `···`, so that I never mistake a missing row for a full tank.
12. As the owner, I want the far skyline to be the Token Skyline's eight weekly bars, so that the scene itself carries the trend.
13. As the owner, I want Total ODO on the stall's LED ticker, so that the running total stays visible.
14. As the owner, I want battery level as a neon battery sign, so that I know the instrument's own charge at a glance.
15. As the owner, I want the battery sign to blink and a `BATTERY n%` chip to appear when battery is low, so that I charge it in time.
16. As the owner, I want the Desk Cat on the stall counter, so that the companion appears on this Page too.
17. As the owner, I want the Desk Cat's tail to swish in HOST, so that the scene feels alive while I am at the desk.
18. As the owner, I want rain, a flickering neon letter and shimmering reflections in HOST, so that the scene has the lofi mood.
19. As the owner, I want the scene perfectly static in ACTIVE and ECO, so that the panel stays crisp and no battery is spent on animation.
20. As the owner, I want a Link failure to switch off every data sign in the scene, so that I can see "no signal" from across the room without reading.
21. As the owner, I want the moon, the clock and the lit stall to stay on during an outage, so that the Page still tells the time.
22. As the owner, I want a Link failure to also show the existing `NO WIFI` / `NO NTP` / `NO CLOCK` chip, so that the state is unambiguous up close.
23. As the owner, I want a Sync failure to keep the signs lit with retained values and show `SYNC FAILED` / `ALLOWANCE` / `TOKEN FAIL`, so that this Page follows the same rule as the others.
24. As the owner, I want Old data to show the `OLD DATA` chip with the M4 collection time, so that I know the bars may be stale.
25. As the owner, I want the Page in both light and dark polarity, so that it looks right however the Deck is set.
26. As the owner, I want light polarity drawn as ink on paper and dark polarity as light on night, so that each polarity uses its own natural idiom.
27. As the owner, I want KEY tap to step to the next Page in the ring, so that navigation behaves as on every other Page.
28. As the owner, I want KEY hold on this Page to pin/unpin the carousel only, so that the behaviour is predictable.
29. As the owner, I want the Page placed after Monument in the ring, so that my existing KEY muscle memory is unchanged.
30. As the owner, I want BOOT (sync now) on this Page to fetch Allowance, Token and Monument, so that I can refresh what the signs show.
31. As the owner, I want the HOST carousel to include this Page every 90 s unless pinned, so that it participates like any other Page.
32. As the owner, I want the Desk Cat console toggle to hide the cat here too, so that one setting controls the cat everywhere.
33. As the owner, I want console `q` on this Page to dump its view model, so that I can debug what the scene shows.
34. As the owner, I want the Page to fetch nothing new, so that it adds no Wi-Fi airtime and no battery cost.
35. As the owner, I want the Page's content fingerprint to stay stable across animation frames, so that the Deck's redraw cache and `data_ts` carry-forward keep working.
36. As the owner, I want the Page to return at the same ring position after a reboot, so that it is not a second-class Page.
37. As the owner, I want the night fiction fixed (moon, stars, rain) regardless of the real hour, so that the Page is a scene, not a weather report — the clock is the only time-of-day truth.
38. As the developer, I want the display model derived purely on-device and encoded as JSON like the other Pages, so that it is natively testable and doubles as the redraw fingerprint.
39. As the developer, I want the scene renderer to be a pure function from (view, polarity, frame) to a 1-bit buffer, so that it can be golden-tested natively without a panel.
40. As the developer, I want golden frames for every state in both polarities, so that art regressions are caught by the native test run.
41. As the developer, I want a test that animation touches only the declared animated regions, so that HOST frames never redraw the whole panel.
42. As the developer, I want a layout test that the longest strings (ODO, caption, chips) fit their boxes, so that nothing collides at worst case.
43. As the developer, I want a footer precedence test for this Page mirroring Station's matrix, so that chip priority is explicit.
44. As the developer, I want the Glossary to define Neon Page and Neon Outage and to amend Desk Cat, so that the vocabulary stays canonical.
45. As the developer, I want the rlcd README to describe the Page, its keys, console commands and states, so that operators can run it.
46. As a future maintainer, I want the billboard pixel font and the sprites as baked bitmap tables like the existing fonts and the Desk Cat, so that the look is reproducible and not hand-tuned per build.

## Implementation Decisions

**Page identity.** Page id `neon-alley`; display model `NeonView`; encoded model schema `rcink.neon-view.v1` carried as the PageRecord's `layout_json`, following the Station Page's `station-view.v2` pattern. Ring: Station ⇄ Allowance ⇄ Token ⇄ Monument ⇄ Neon.

**No new data path.** The Page fetches nothing. It derives from the already-held Allowance, Token and Monument snapshots with their sync states, the battery reading, the SHTC3 reading, the RTC wall clock, the global dark-polarity flag and the Desk Cat enabled flag. BOOT on this Page runs the existing Allowance + Token + Monument sync.

**NeonView** (all fields participate in equality; the encoded form is the fingerprint):

```text
clock_hhmm            "--:--" when the wall clock is invalid
weekday, day          billboard caption
env_temp              billboard caption (Ambient Chrome; humidity is not shown on this Page)
providers[0..4]       { id_initial, left | unavailable, low_fuel }  in Allowance Page order
skyline_bars[8]       the existing Token Skyline derivation at the 8W scale
total_odo             formatted like Station's Total ODO
battery_pct, battery_low
outage                true iff a Link failure is present
footer_chip, footer_text   Station's composite precedence and wording
cat_enabled
```

**Footer precedence reuses the Station Page's composite matrix** (SYNCING → NO WIFI → NO NTP → NO CLOCK → SYNC FAILED → ALLOWANCE → TOKEN FAIL → OLD DATA) over the same three feeds with the same retry wording. `BATTERY n%` is the lowest-precedence chip and appears only when nothing else applies and battery is low.

**Neon Outage ⇔ Link failure only.** When `outage` is set, the FUEL rows, battery sign, ODO ticker, lanterns, OPEN and 24H signs are drawn as dashed outlines; the moon, stall interior, doorway and vending machine stay lit; the billboard reads `--:--` if the clock is invalid. Sync failure and Old data do **not** cause an outage: signs stay lit with retained values and the chip carries the state.

This deliberately diverges from PRODUCT principle 4 ("…without hiding retained values"). On this Page the clock is the job; retained telemetry stays one KEY tap away on Allowance / Token / Monument, and an unmistakable outage is worth more here than dimmed stale bars.

**Low fuel** uses the same predicate as the Station rows (`low_fuel`); the bar blinks at 2 Hz in HOST and the `!` is steady. **Battery low** uses the device's existing `battery_ok == false`; the sign blinks at 1 Hz in HOST and the `BATTERY n%` chip appears.

**Keys, console, persistence.** KEY tap: next Page. KEY hold on Neon: pin/unpin only (no Page-specific cycle). KEY + BOOT chord: pin/unpin as everywhere. Console `k` (Desk Cat) applies; `q` dumps the view; `w` is a no-op here. No new NVS keys; the existing page-index persistence must cope with the ring growing from four to five Pages (a stored index beyond the new count clamps to Station).

**Animation** (HOST only, 8 FPS, the same quiet-frame loop the Desk Cat, Skyline sky and sweeps already use): rain streaks confined to the sky band above the skyline; Desk Cat tail pose alternates at 1 Hz; exactly one letter each on the FUEL (`U`) and OPEN (`E`) signs flickers on fixed periods; the billboard colon blinks at 1 Hz; reflection streaks jitter horizontally by 1 px. ACTIVE and ECO render frame 0 deterministically: fixed sparse rain, colon on, tail pose A, no flicker, no jitter. Animation state lives outside `NeonView`, so the fingerprint never changes across frames.

**Polarity.** The Page follows the global dark-polarity flag; there is no per-Page polarity. The scene renderer takes polarity as an input and emits the final panel image — light: paper sky, solid-ink skyline, 50 % ink-dither walls, ink-dash street; dark: ink sky, 25 % light-dot skyline, 50 % light-dither walls, light-dash street. Because the renderer already emits the final image, this Page must **not** be inverted again at pack time; it is packed as light while every other Page keeps the existing inversion. Both looks were approved in the mock.

**Fixed night fiction.** Moon, stars and rain are drawn regardless of the real hour; no SkyPhase coupling.

**Density shipped = the mock's L1.** Signs plus billboard caption; no standard header/footer bar. The mock's L0 (scene only) and L2 (standard chrome) are not shipped. Chips appear only in failure / low-battery states, in the Deck's chip grammar, on the street band.

**Typography.** A new baked 5×7 pixel font (digits, uppercase, `% . : ! - / °`) drawn at ×5 for the billboard (25×35 px glyphs), ×2 for signs, ticker and chips, ×1 for `!`. The Barlow fonts are not used on this Page.

**Scene structure** (datums from the mock): sky 0–130 with the eight skyline bars on the 130 baseline; city block 130–266 at 50 % dither carrying the FUEL sign (left), lanterns / battery / OPEN sign / vending machine (right), the 24H sign and noren doorway (centre-right); the stall 184–266 with striped awning, lit interior (worker at a laptop, two hanging lamps, steaming cup), the Desk Cat at 2×, counter and ODO ticker; street 266–300 with dash texture, 2 px kerb, a reflection streak under every light (solid at the kerb, breaking into dashes), a puddle and a drain grate; the billboard 116–284 × 40–104 on two posts with an X-brace and a dither halo.

**Sprites** — the worker at the laptop (42×44), the Desk Cat SIT pose (15×10, two tail poses) — are baked bitmap tables in the style of the Desk Cat's.

**Glossary.** Add **Neon Page** and **Neon Outage**; amend **Desk Cat** to cover its 2× Neon perch; Ambient Chrome is unchanged (the billboard caption is an instance of it).

**README.** rlcd operator notes gain the Page: ring position, key behaviour, console keys, failure states, polarity behaviour.

## Testing Decisions

A good test asserts on external behaviour only: the encoded view for given snapshots, sync states and clock; the chip text; which signs are outlined versus lit; the pixels of golden frames; that animation changes only the declared regions; that the longest strings fit. No test asserts on private drawing calls or on TFT objects.

Two seams, both exercised under the native test environment:

1. **View-model seam (existing pattern).** `derive_neon_view(…)` → `NeonView`, with `encode_neon_view` / `decode_neon_view`. Tested the way Station is: footer precedence matrix; outage ⇔ Link failure and nothing else; low-fuel predicate; unavailable rows; skyline bars; battery-low flag; codec round trip; ECO determinism (identical inputs → identical encoding).
2. **Scene seam (the one new seam).** `render_neon_scene(view, polarity, frame, out)` — a pure function into a plain 400×300 1-bit buffer with no TFT dependency, so it runs natively. Golden frames per state (ok, outage, sync failure, old data, low battery, no clock) × polarity (light, dark) live as fixtures and are compared by hash or PNG; the Arduino layer blits this buffer into the sprite.

Prior art: the Station native test (precedence matrix, ECO determinism), the Desk Cat, mono-pack and Station-layout native tests, and the native fixtures directory.

Specific tests:

- **Animation-bounded:** render frames 0 and 7 for the same view; mask the declared animated regions (sky band, cat rectangle, FUEL `U` cell, OPEN `E` cell, colon cells, street band); the masked buffers must be identical.
- **Layout fit:** `ODO 999.9B`, the widest caption (`WED 31  -10.5°`) and the longest chip fit their boxes without overlap; four Provider rows fit the sign.
- **Polarity pair:** the light and dark golden frames for the same view are both checked in, not one derived from the other, because the two idioms differ (moon ring vs disc, solid vs dotted skyline).

## Out of Scope

- Any new producer feed or HTTPS client (agenda, weather, GitHub, live agent sessions).
- The mock's L0 and L2 densities; a standard header/footer bar on this Page.
- Real SkyPhase / time-of-day sky; weather.
- The sill-clock variant.
- CJK neon glyphs (the rlcd fonts are Latin-only).
- Heatmap, streak, Seven-Day Total, today's tokens and humidity on this Page.
- Per-Page polarity, carousel changes, sound.
- Any change to the other four Pages' renderers.

## Further Notes

- **Name.** "Neon Page" (header text `NEON` in the mock) is a proposal that fits the Deck's single-noun convention (Station / Allowance / Token / Monument). Rename before implementation if a better noun turns up.
- The mock's `WIFI FAILED` label was a placeholder; the implementation uses the Deck's real `NO WIFI` / `NO NTP` / `NO CLOCK` chips.
- The owner approved, in order: the white-paper and night looks, the L1 density, the billboard clock, the Neon Outage metaphor, the dither-wall revision with hand-drawn worker and cat sprites, and the wet-street revision.
- Suggested implementation order: view model + codec + native tests → scene renderer + golden tests → wiring (ring, keys, console, animation loop, pack-without-inversion) → README + Glossary.
