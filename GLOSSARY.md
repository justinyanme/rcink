# rcink RLCD Domain Language

Canonical domain vocabulary for the Waveshare ESP32-S3-RLCD-4.2 desktop AI instrument and its telemetry views.

## Language

**Deck**:
The ordered ring of live dashboard pages cycled by the `KEY` button and `HOST` auto-carousel.
_Avoid_: Carousel list, tab bar, app screens

**Allowance Page**:
The primary Deck page displaying per-provider remaining weekly subscription capacity, burn pace relative to reset, and expiry risk.
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

**Primary Provider**:
A subscription pool actively tracked across both allowance and daily token telemetry (`Claude`, `Codex`, `Gemini`), prioritized over secondary backup pools when recommending a provider.
_Avoid_: Main tier, default account, active vendor

**Token Page**:
The secondary Deck page displaying per-provider 7-day consumption totals, today's share against a usual day, a 30-day activity chart, and lifetime odometers.
_Avoid_: Metrics screen, billing view, cost page

**Active-Window Usual Day**:
The daily baseline mean calculated over a provider's complete days within the prior 7-day window starting from its first active day in the 30-day series, excluding pre-onboarding zero days.
_Avoid_: 30-day average, rolling mean, historical quota

**Odometer**:
A monotonic cumulative token total counted per provider (`ODO`) or across all providers (`Total ODO`) since tracking began.
_Avoid_: Counter, lifetime gauge, mileage

**Monument Page**:
The third Deck page combining the desktop wall clock, 53×7 annual activity heatmap, Total Odometer milestone progress, and indoor micro-climate strip.
_Avoid_: Heatmap view, climate screen, clock tab

**Ambient Chrome**:
Compact room temperature and humidity telemetry embedded cleanly in page headers so the owner can glance at desk climate from any view without a dedicated thermometer screen.
_Avoid_: Environment header, sensor widget, status climate

**Host Carousel**:
Timed automatic page rotation occurring every 90 seconds only when USB `HOST` data power is connected and the user has not pinned the current page.
_Avoid_: Slideshow, auto-scroll, screensaver
