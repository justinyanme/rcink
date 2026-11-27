# rcink RLCD Domain Language

Canonical domain vocabulary for the Waveshare ESP32-S3-RLCD-4.2 desktop AI instrument and its telemetry views. For device runtime modes, e-ink refresh waveforms, and connectivity failure states, see `CONTEXT.md`.

## Language

**Deck**:
The ordered ring of live dashboard pages cycled by the `KEY` button and `HOST` auto-carousel.
_Avoid_: Carousel list, tab bar, app screens

**Provider**:
An AI service whose Allowance or Token Usage the instrument tracks under the same name across Deck pages. Historical Token Usage belongs to the Provider that incurred it, including after its subscription ends.
_Avoid_: Vendor, model, Claude Code, Antigravity, Gemini CLI

**Primary Provider**:
A subscription pool prioritized in recommendations and headlines. Claude, Codex and Gemini are Primary Providers. Z.AI and Kimi are secondary Providers.
_Avoid_: Main tier, default account, active vendor

**Allowance**:
The unused share of a Provider's weekly subscription limit, scoped to a single account and never summed across machines.
_Avoid_: Quota, limit, usage

**Station Page**:
The home Deck page uniting the desktop wall clock, Total Odometer, rolling activity heatmap paired with the Token Skyline, ambient desk climate, compact per-provider Allowance and Seven-Day Total telemetry, and the Desk Cat roaming its horizontal divider ledges in `HOST` mode.
_Avoid_: Dashboard view, summary screen, overview tab, all-in-one page

**Allowance Page**:
The dedicated Deck page displaying per-provider remaining weekly subscription capacity, burn pace relative to reset, and expiry risk.
_Avoid_: Limits screen, quota tab, usage road

**Even Pace**:
The linear share of a weekly allowance window remaining before its next reset.
_Avoid_: Average speed, benchmark, target bar

**Headroom**:
The percentage-point margin between a provider's remaining allowance and Even Pace (`remaining − even pace`).
_Avoid_: Slack, delta, buffer

**Expiring Surplus**:
A live provider within 36 hours of reset holding at least 25% remaining allowance and at least +10 percentage points of Headroom.
_Avoid_: Wasted quota, expiring balance, urgent limit

**Token Usage**:
Tokens a Provider's CLI consumed in a local calendar day, summed across all reporting Sources.
_Avoid_: Usage (alone), consumption, spend

**Source**:
A machine that collects and uploads its local Token Usage, such as the M4 (`mac-mini`) or MacBook.
_Avoid_: Device, host

**Seven-Day Total**:
Token Usage summed over today and the six preceding local days, independent of any Allowance reset window.
_Avoid_: This week, weekly usage

**Token Page**:
The dedicated Deck page displaying up to four active Providers with per-provider seven-day consumption totals, today's share against an Active-Window Usual Day, a 30-day activity chart, and lifetime odometers.
_Avoid_: Metrics screen, billing view, cost page

**Active-Window Usual Day**:
The daily baseline mean calculated over a provider's complete days within the prior 7-day window starting from its first active day in the 30-day series, excluding pre-onboarding zero days.
_Avoid_: Usual day (unqualified), 30-day average, rolling mean, historical quota

**Odometer**:
A monotonic cumulative token total counted per Provider (`ODO`) or across all Providers (`Total ODO`) since tracking began (`2025-09-18`). Total ODO includes Token Usage from past subscriptions.
_Avoid_: Lifetime, counter, lifetime gauge, mileage

**Monument Page**:
The dedicated Deck page combining the desktop wall clock, rolling activity heatmap, Total Odometer milestone progress, and indoor micro-climate strip.
_Avoid_: Dashboard view, heatmap view, climate screen, clock tab

**Neon Page**:
The fifth Deck page, a fixed night alley with an RTC billboard clock. Its FUEL sign shows Allowance, the eight distant buildings show weekly Token Usage, and the stall ticker shows Total ODO.

**Neon Outage**:
The Neon Page's response to a Link failure. The data signs and lanterns go dark with dashed outlines. The billboard, moon, stall, doorway and vending machine stay lit.

**Heatmap Window**:
The user-selected rolling lookback span (`1Y`, `6M`, or `3M`) ending today. On Monument Page (`1Y` default) it frames the full-width activity heatmap and window summary metrics; on Station Page (`6M` default) it couples the left rolling heatmap with the right Token Skyline scale (`6M ↔ 8W`, `3M ↔ 7D`, `1Y ↔ 6M`).
_Avoid_: Date filter, zoom level, calendar mode

**Token Skyline**:
The multi-scale 1-bit pixel-city bar chart beside the Station Page heatmap showing rolling `8W` (weekly), `7D` (daily), or `6M` (monthly) Token Usage against a prior-period dashed baseline and `WoW`/`MoM` delta chip, backed by a local-RTC Dawn/Day/Dusk/Night sky.
_Avoid_: Mini chart, city widget, bar graph, skyline plot

**Desk Cat**:
The `15×10 px` 1-bit pixel companion roaming Station Page's three horizontal divider ledges (`y = 28`, `y = 151`, `y = 272`) at 8 FPS in USB `HOST` mode. On Neon Page it sits at 2× on the stall counter and swishes its tail. In `ACTIVE` and `ECO` it rests in a static `SIT` pose on both Pages.
_Avoid_: Virtual pet, mascot, screensaver cat

**Ambient Chrome**:
Compact room temperature and humidity telemetry embedded cleanly in page headers so the owner can glance at desk climate from any view without a dedicated thermometer screen.
_Avoid_: Environment header, sensor widget, status climate

**Host Carousel**:
Timed automatic page rotation occurring every 90 seconds only when USB `HOST` data power is connected and the user has not pinned the current page.
_Avoid_: Slideshow, auto-scroll, screensaver
