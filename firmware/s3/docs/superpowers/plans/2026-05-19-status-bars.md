# Top status bar + slim bottom nav — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a firmware-painted top status bar (battery glyph + temp + humidity + data time + refresh time), slim the bottom indicator to a single nav row that includes the current page name, and delete the redundant per-page footer row from each producer page.

**Architecture:** Two firmware-only overlays sandwich the producer's layout viewport. New `src/sense/` modules read SHT40 (temp/humidity) and M5PM1 (battery) at wake; readings render geometrically (battery) or with newly-baked Lucide icons (thermometer / droplet / download / refresh). Producer pages lose their trailing brand-icon + page-name footer since the bottom strip now carries the current page name.

**Tech Stack:** C++17 (PlatformIO + M5Unified/M5GFX/M5PM1), Unity for native firmware tests, Python 3.13 + pytest for producer.

**Spec:** `docs/superpowers/specs/2026-05-19-status-bars-design.md`.

---

## File map

| File | Action | Purpose |
|---|---|---|
| `src/sense/battery.h` | create | `BatteryReading` struct + `read_battery()` API |
| `src/sense/battery.cpp` | create | M5PM1 `readVbat` + LUT mV→% |
| `src/sense/env.h` | create | `EnvReading` struct + `read_sht40()` API; pure-logic helpers `sht_crc8` + `sht_convert_raw` exposed for native tests |
| `src/sense/env.cpp` | create | I2C transaction (`M5.In_I2C`) wrapping the pure helpers |
| `test/test_native_battery/test_main.cpp` | create | Unity tests for LUT |
| `test/test_native_env/test_main.cpp` | create | Unity tests for CRC + conversion |
| `platformio.ini` | modify | extend `build_src_filter` to include `+<sense/>` for native env |
| `tools/icons/src/thermometer.svg` | create | Lucide source |
| `tools/icons/src/droplet.svg` | create | Lucide source |
| `tools/icons/src/download.svg` | create | Lucide source |
| `tools/icons/src/refresh-cw.svg` | create | Lucide source |
| `tools/icons/bake.py` | modify | add 4 entries to `NAME_TO_SVG` |
| `src/render/icons_data.h` | regenerate | bake output |
| `src/main.cpp` | modify | add `paint_status_bar`, slim `paint_indicator`, reserve top + bottom viewport, call sensor reads on setup/refetch |
| `producer/rcpaper/pages/overview.py` | modify | remove trailing footer hstack + its divider |
| `producer/rcpaper/pages/claude.py` | modify | remove trailing footer hstack + its divider |
| `producer/rcpaper/pages/codex.py` | modify | remove trailing footer hstack + its divider |
| `producer/rcpaper/pages/calendar.py` | modify | remove trailing footer hstack + its divider |
| `producer/rcpaper/pages/todo.py` | modify | remove trailing brand footer hstack (keep DONE progress row — it's content) |
| `producer/tests/test_pages_claude.py` | modify | delete `test_footer_includes_brand_and_plan` |

---

## Phase 1 — Battery module (TDD, native-testable)

### Task 1: Failing test for battery LUT

**Files:**
- Create: `test/test_native_battery/test_main.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <unity.h>
#include "sense/battery.h"

using namespace rcpaper;

void test_battery_lut_anchors() {
  TEST_ASSERT_EQUAL(0,   battery_mv_to_pct(3000));
  TEST_ASSERT_EQUAL(5,   battery_mv_to_pct(3300));
  TEST_ASSERT_EQUAL(15,  battery_mv_to_pct(3500));
  TEST_ASSERT_EQUAL(30,  battery_mv_to_pct(3650));
  TEST_ASSERT_EQUAL(45,  battery_mv_to_pct(3750));
  TEST_ASSERT_EQUAL(60,  battery_mv_to_pct(3850));
  TEST_ASSERT_EQUAL(75,  battery_mv_to_pct(3950));
  TEST_ASSERT_EQUAL(90,  battery_mv_to_pct(4050));
  TEST_ASSERT_EQUAL(100, battery_mv_to_pct(4200));
}

void test_battery_lut_interpolates() {
  // Halfway between 3950 (75%) and 4050 (90%) → ~82.5% → rounds to 82 or 83.
  uint8_t mid = battery_mv_to_pct(4000);
  TEST_ASSERT(mid >= 82 && mid <= 83);
}

void test_battery_lut_clamps() {
  TEST_ASSERT_EQUAL(0,   battery_mv_to_pct(2000));
  TEST_ASSERT_EQUAL(100, battery_mv_to_pct(4500));
  TEST_ASSERT_EQUAL(0,   battery_mv_to_pct(0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_battery_lut_anchors);
  RUN_TEST(test_battery_lut_interpolates);
  RUN_TEST(test_battery_lut_clamps);
  return UNITY_END();
}
```

- [ ] **Step 2: Update platformio.ini native filter**

```
# In [env:native], change build_src_filter from:
#   build_src_filter = -<main.cpp> +<render/> +<proto/>
# to:
build_src_filter = -<main.cpp> +<render/> +<proto/> +<sense/battery.cpp>
```

Don't add `+<sense/env.cpp>` yet — that pulls Arduino/I2C headers we'll
gate later. Only the battery LUT cpp is pure logic right now.

- [ ] **Step 3: Run the test and confirm it fails**

```bash
pio test -e native -f test_native_battery
```
Expected: build failure (`sense/battery.h` not found).

### Task 2: Battery LUT implementation

**Files:**
- Create: `src/sense/battery.h`, `src/sense/battery.cpp`

- [ ] **Step 1: Write `src/sense/battery.h`**

```cpp
// src/sense/battery.h
#pragma once
#include <cstdint>

namespace rcpaper {

struct BatteryReading {
  uint8_t  pct;     // 0..100
  uint16_t mv;
  bool     ok;     // false on read failure (pct/mv may be 0)
};

// Pure-logic LUT mapping Li-ion cell voltage (mV) to charge percent.
// 9-point piecewise linear: 3000/3300/3500/3650/3750/3850/3950/4050/4200 mV
// ↔ 0/5/15/30/45/60/75/90/100 %. Out-of-range values clamp.
uint8_t battery_mv_to_pct(uint16_t mv);

// Reads battery voltage from M5PM1 and maps to percent. Device-only —
// only compiled under ARDUINO.
BatteryReading read_battery();

}  // namespace rcpaper
```

- [ ] **Step 2: Write `src/sense/battery.cpp`**

```cpp
// src/sense/battery.cpp
#include "sense/battery.h"

#ifdef ARDUINO
#include <M5Unified.h>
#endif

namespace rcpaper {

namespace {
constexpr uint16_t kAnchorMv[]  = {3000, 3300, 3500, 3650, 3750, 3850, 3950, 4050, 4200};
constexpr uint8_t  kAnchorPct[] = {   0,    5,   15,   30,   45,   60,   75,   90,  100};
constexpr int kN = sizeof(kAnchorPct) / sizeof(kAnchorPct[0]);
}  // namespace

uint8_t battery_mv_to_pct(uint16_t mv) {
  if (mv <= kAnchorMv[0])      return kAnchorPct[0];
  if (mv >= kAnchorMv[kN - 1]) return kAnchorPct[kN - 1];
  for (int i = 1; i < kN; ++i) {
    if (mv <= kAnchorMv[i]) {
      uint16_t lo_mv  = kAnchorMv[i - 1];
      uint16_t hi_mv  = kAnchorMv[i];
      uint8_t  lo_pct = kAnchorPct[i - 1];
      uint8_t  hi_pct = kAnchorPct[i];
      uint32_t span_mv  = hi_mv - lo_mv;
      uint32_t delta_mv = mv - lo_mv;
      uint32_t span_pct = hi_pct - lo_pct;
      return (uint8_t)(lo_pct + (delta_mv * span_pct) / span_mv);
    }
  }
  return kAnchorPct[kN - 1];  // unreachable
}

#ifdef ARDUINO
// Read VBAT directly from the PM1 (0x6E) registers 0x22/0x23 over the
// existing M5.In_I2C bus. Matches the pattern used in power/sleep.cpp,
// which also pokes PM1 via M5.In_I2C without instantiating the M5PM1 lib.
BatteryReading read_battery() {
  BatteryReading r{0, 0, false};
  uint8_t lo = 0, hi = 0;
  constexpr uint8_t kAddr = 0x6E;
  constexpr uint32_t kFreqHz = 100000;
  if (!M5.In_I2C.readRegister(kAddr, 0x22, &lo, 1, kFreqHz)) return r;
  if (!M5.In_I2C.readRegister(kAddr, 0x23, &hi, 1, kFreqHz)) return r;
  uint16_t mv = (uint16_t)lo | ((uint16_t)(hi & 0x0F) << 8);
  r.mv  = mv;
  r.pct = battery_mv_to_pct(mv);
  r.ok  = true;
  return r;
}
#endif

}  // namespace rcpaper
```

- [ ] **Step 3: Run the native tests and confirm they pass**

```bash
pio test -e native -f test_native_battery
```
Expected: 3/3 PASS.

- [ ] **Step 4: Commit**

```bash
git add src/sense/battery.h src/sense/battery.cpp \
        test/test_native_battery/test_main.cpp \
        platformio.ini
git commit -m "feat(sense): battery LUT (mV → %) + M5PM1 wrapper"
```

---

## Phase 2 — SHT40 env module (TDD, native-testable)

### Task 3: Failing test for SHT40 CRC + raw conversion

**Files:**
- Create: `test/test_native_env/test_main.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <unity.h>
#include "sense/env.h"

using namespace rcpaper;

void test_sht_crc8_known_vector() {
  // SHT4x datasheet appendix: CRC of {0xBE, 0xEF} is 0x92.
  const uint8_t data[] = {0xBE, 0xEF};
  TEST_ASSERT_EQUAL_HEX8(0x92, sht_crc8(data, 2));
}

void test_sht_convert_temperature_bounds() {
  // raw = 0x0000 → -45.0 °C; raw = 0xFFFF → +130.0 °C
  float t, rh;
  sht_convert_raw(0x0000, 0x8000, t, rh);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -45.0f, t);
  sht_convert_raw(0xFFFF, 0x8000, t, rh);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 130.0f, t);
}

void test_sht_convert_humidity_bounds_and_clamp() {
  float t, rh;
  // raw_h = 0x8000 → ~50%
  sht_convert_raw(0x4000, 0x8000, t, rh);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, rh);

  // raw_h = 0xFFFF → 100% (datasheet clamp; raw 0xFFFF maps to 100.0 exactly)
  sht_convert_raw(0x4000, 0xFFFF, t, rh);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, rh);

  // raw_h = 0x0000 → 0% (clamp; the linear formula yields a small negative)
  sht_convert_raw(0x4000, 0x0000, t, rh);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, rh);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sht_crc8_known_vector);
  RUN_TEST(test_sht_convert_temperature_bounds);
  RUN_TEST(test_sht_convert_humidity_bounds_and_clamp);
  return UNITY_END();
}
```

- [ ] **Step 2: Extend native build filter**

```
# In platformio.ini [env:native], extend build_src_filter to also include
# the env cpp — but only the pure helpers compile natively because of an
# `#ifdef ARDUINO` guard around the I2C path:
build_src_filter = -<main.cpp> +<render/> +<proto/> +<sense/>
```

- [ ] **Step 3: Run the test and confirm it fails**

```bash
pio test -e native -f test_native_env
```
Expected: build failure (`sense/env.h` not found).

### Task 4: SHT40 module — pure helpers + Arduino I2C wrapper

**Files:**
- Create: `src/sense/env.h`, `src/sense/env.cpp`

- [ ] **Step 1: Write `src/sense/env.h`**

```cpp
// src/sense/env.h
#pragma once
#include <cstdint>
#include <cstddef>

namespace rcpaper {

struct EnvReading {
  float temp_c;       // valid when ok == true
  float humidity_pct; // valid when ok == true; 0..100
  bool  ok;
};

// CRC-8 (poly 0x31, init 0xFF, no reflect, no final xor) per SHT4x spec.
// Exposed for unit tests.
uint8_t sht_crc8(const uint8_t* data, size_t len);

// Map raw 16-bit ticks from the SHT40 read response to physical units.
// Datasheet:
//   t_c          = -45 + 175 * raw_t / 65535
//   humidity_pct = -6 + 125 * raw_h / 65535, clamped to [0, 100]
// Exposed for unit tests.
void sht_convert_raw(uint16_t raw_t, uint16_t raw_h, float& t_c, float& rh_pct);

// Device-only: single-shot high-precision read at I2C 0x44.
// Returns ok=false on NACK, CRC fail, or timeout (3 in-place retries).
EnvReading read_sht40();

}  // namespace rcpaper
```

- [ ] **Step 2: Write `src/sense/env.cpp`**

```cpp
// src/sense/env.cpp
#include "sense/env.h"

#ifdef ARDUINO
#include <M5Unified.h>
#include <Arduino.h>
#endif

namespace rcpaper {

uint8_t sht_crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

void sht_convert_raw(uint16_t raw_t, uint16_t raw_h, float& t_c, float& rh_pct) {
  t_c = -45.0f + 175.0f * (float)raw_t / 65535.0f;
  float rh = -6.0f + 125.0f * (float)raw_h / 65535.0f;
  if (rh < 0.0f) rh = 0.0f;
  if (rh > 100.0f) rh = 100.0f;
  rh_pct = rh;
}

#ifdef ARDUINO

static constexpr uint8_t kSht40Addr      = 0x44;
static constexpr uint8_t kSht40CmdHighRes = 0xFD;  // T+RH, high precision

EnvReading read_sht40() {
  EnvReading r{0.0f, 0.0f, false};
  constexpr uint32_t kFreqHz = 100000;
  for (int attempt = 0; attempt < 3; ++attempt) {
    // SHT40 has no register concept — send a single command byte.
    if (!M5.In_I2C.start(kSht40Addr, false, kFreqHz)
        || !M5.In_I2C.write(kSht40CmdHighRes)
        || !M5.In_I2C.stop()) {
      delay(5);
      continue;
    }
    delay(12);  // High-precision conversion is ~8.2 ms per datasheet.
    uint8_t buf[6] = {0};
    if (!M5.In_I2C.start(kSht40Addr, true, kFreqHz)
        || !M5.In_I2C.read(buf, sizeof(buf), true)
        || !M5.In_I2C.stop()) {
      delay(5);
      continue;
    }
    if (sht_crc8(&buf[0], 2) != buf[2] || sht_crc8(&buf[3], 2) != buf[5]) {
      delay(5);
      continue;
    }
    uint16_t raw_t = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t raw_h = ((uint16_t)buf[3] << 8) | buf[4];
    sht_convert_raw(raw_t, raw_h, r.temp_c, r.humidity_pct);
    r.ok = true;
    return r;
  }
  return r;
}

#endif  // ARDUINO

}  // namespace rcpaper
```

- [ ] **Step 3: Run native tests and confirm pass**

```bash
pio test -e native -f test_native_env
```
Expected: 3/3 PASS.

- [ ] **Step 4: Confirm device build still compiles**

```bash
pio run -e m5stack-papercolor
```
Expected: build OK. (The new sources aren't yet referenced by `main.cpp`, but they must compile cleanly under both targets.)

- [ ] **Step 5: Commit**

```bash
git add src/sense/env.h src/sense/env.cpp \
        test/test_native_env/test_main.cpp \
        platformio.ini
git commit -m "feat(sense): SHT40 driver — CRC + raw conversion + I2C wrapper"
```

---

## Phase 3 — Icon assets

### Task 5: Bake four new Lucide icons

**Files:**
- Create: `tools/icons/src/thermometer.svg`, `tools/icons/src/droplet.svg`, `tools/icons/src/download.svg`, `tools/icons/src/refresh-cw.svg`
- Modify: `tools/icons/bake.py`
- Regenerate: `src/render/icons_data.h`

- [ ] **Step 1: Drop the four SVGs**

Source from Lucide (https://lucide.dev). Each SVG is 24×24, stroke-based.
Save the raw SVG markup verbatim:

`tools/icons/src/thermometer.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 4v10.54a4 4 0 1 1-4 0V4a2 2 0 0 1 4 0Z"/></svg>
```

`tools/icons/src/droplet.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 22a7 7 0 0 0 7-7c0-2-1-3.9-3-5.5s-3.5-4-4-6.5c-.5 2.5-2 4.9-4 6.5C6 11.1 5 13 5 15a7 7 0 0 0 7 7z"/></svg>
```

`tools/icons/src/download.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" x2="12" y1="15" y2="3"/></svg>
```

`tools/icons/src/refresh-cw.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/><path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16"/><path d="M8 16H3v5"/></svg>
```

- [ ] **Step 2: Extend `NAME_TO_SVG` in `tools/icons/bake.py`**

```python
NAME_TO_SVG = {
    "claude": "asterisk",
    "codex": "code",
    "calendar": "calendar",
    "todo": "square-check-big",
    "quote": "quote",
    "link": "link",
    "weather": "sun",
    "sun": "sun",
    "bell": "bell",
    "clock": "clock",
    "github": "github-logo",
    "external": "external-link",
    # ---- new for top status bar ----
    "thermometer": "thermometer",
    "droplet": "droplet",
    "download": "download",
    "refresh": "refresh-cw",
}
```

- [ ] **Step 3: Run the bake**

```bash
brew list librsvg >/dev/null 2>&1 || brew install librsvg
uv run --with pillow python tools/icons/bake.py
```
Expected: `wrote .../src/render/icons_data.h  (15 glyphs × 2 sizes)`.

- [ ] **Step 4: Confirm device build still passes**

```bash
pio run -e m5stack-papercolor
```
Expected: build OK.

- [ ] **Step 5: Commit**

```bash
git add tools/icons/src/*.svg tools/icons/png/*.png tools/icons/bake.py \
        src/render/icons_data.h
git commit -m "feat(icons): bake thermometer/droplet/download/refresh for status bar"
```

---

## Phase 4 — Firmware overlays

### Task 6: Reserve top viewport + transient sensor globals

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add includes and transient globals at the top of `main.cpp`**

Find the existing block:

```cpp
#include "store/nvs.h"
#include "power/sleep.h"
```

Replace with:

```cpp
#include "store/nvs.h"
#include "power/sleep.h"
#include "sense/battery.h"
#include "sense/env.h"
```

Then locate the existing static globals:

```cpp
// Persistent across the dev-console loop. Loaded once in setup().
static DeviceState g_state;
```

Add directly below:

```cpp
// Transient — re-read every wake / refetch. Not persisted in NVS.
static BatteryReading g_battery{};
static EnvReading     g_env{};
```

- [ ] **Step 2: Wire reads into `setup()` and `refetch_now()`**

In `setup()` (search for `M5.begin` call), after the existing
`M5.begin(...)` line and before the first render, add:

```cpp
  g_battery = read_battery();
  g_env     = read_sht40();
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d  env=%.1fC %.0f%% ok=%d\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok,
                g_env.temp_c, g_env.humidity_pct, g_env.ok);
```

In `refetch_now()` (existing function), at the very start of the function
body, add:

```cpp
  g_battery = read_battery();
  g_env     = read_sht40();
```

- [ ] **Step 3: Build and confirm it compiles**

```bash
pio run -e m5stack-papercolor
```
Expected: build OK.

- [ ] **Step 4: Commit (no UI change yet)**

```bash
git add src/main.cpp
git commit -m "feat(fw): read battery + SHT40 on wake and refetch"
```

### Task 7: Slim the bottom indicator to a single row + add current page name

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Update strip constants**

Locate:

```cpp
static constexpr int TS_H  = 14;
static constexpr int NAV_H = 22;
static constexpr int IND_STRIP_H = TS_H + NAV_H;
```

Replace with:

```cpp
static constexpr int BOT_STRIP_H = 22;  // single nav row; no more data/refresh row
static constexpr int TOP_STRIP_H = 22;  // status bar — battery, env, freshness
```

Leave `format_local_hhmm` in place — it's already a free function above
`paint_indicator` and the new `paint_status_bar` (Task 8) will reuse it.

- [ ] **Step 2: Rewrite `paint_indicator` to single-row**

Replace the entire `paint_indicator` function with:

```cpp
static void paint_indicator(M5Canvas& canvas,
                            const std::vector<PageRecord>& pages, uint8_t idx) {
  if (pages.empty()) return;
  const int16_t W = canvas.width();
  const int16_t H = canvas.height();
  const int16_t y_strip = H - BOT_STRIP_H;

  // White background + hairline top rule
  canvas.fillRect(0, y_strip, W, BOT_STRIP_H, 0xFFFFFF);
  canvas.drawFastHLine(0, y_strip, W, 0x000000);

  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(0x000000);

  const size_t n = pages.size();
  const uint8_t prev_idx = (uint8_t)((idx + n - 1) % n);
  const uint8_t next_idx = (uint8_t)((idx + 1) % n);

  auto upper = [](const String& s) {
    String out = s;
    out.toUpperCase();
    return out;
  };
  const String prev_id = upper(pages[prev_idx].id);
  const String next_id = upper(pages[next_idx].id);
  const String curr_id = upper(pages[idx].id);

  const int16_t y_text = y_strip + 4;
  const int16_t y_mid  = y_strip + BOT_STRIP_H / 2;

  // Left: ◀ PREV
  const int16_t lpad = 8;
  canvas.fillTriangle(lpad,     y_mid,
                      lpad + 7, y_strip + 5,
                      lpad + 7, y_strip + BOT_STRIP_H - 5, 0x000000);
  canvas.setCursor(lpad + 13, y_text);
  canvas.print(prev_id.c_str());

  // Right: NEXT ▶
  const int16_t rpad = 8;
  const int16_t arrow_x = W - rpad - 7;
  canvas.fillTriangle(arrow_x,     y_strip + 5,
                      arrow_x,     y_strip + BOT_STRIP_H - 5,
                      arrow_x + 7, y_mid, 0x000000);
  const int16_t next_w = canvas.textWidth(next_id.c_str());
  canvas.setCursor(arrow_x - 5 - next_w, y_text);
  canvas.print(next_id.c_str());

  // Center: pagination dots + current page name.
  // Layout: [dots]  [gap]  CURRENT
  const int16_t dot_gap = 10;
  const int16_t dots_w  = (n > 1) ? (int16_t)((n - 1) * dot_gap) : 0;
  const int16_t curr_w  = canvas.textWidth(curr_id.c_str());
  const int16_t group_w = dots_w + (n > 1 ? 12 : 0) + curr_w;
  int16_t dx = (W - group_w) / 2;
  for (size_t i = 0; i < n; ++i) {
    if (i == idx) canvas.fillCircle(dx, y_mid, 3, 0x000000);
    else          canvas.drawCircle(dx, y_mid, 2, 0x000000);
    dx += dot_gap;
  }
  // After dots, draw the current page name to the right.
  int16_t name_x = (n > 1) ? (dx - dot_gap + 12) : (W - curr_w) / 2;
  canvas.setCursor(name_x, y_text);
  canvas.print(curr_id.c_str());
}
```

- [ ] **Step 3: Update the caller in `render_page`**

Locate the existing call:

```cpp
  // Overlay the page-rotation indicator strip at the bottom of every page.
  paint_indicator(canvas, g_state.pages, g_state.page_index,
                  g_state.last_fetch_s);
```

Replace with:

```cpp
  // Overlay the page-rotation indicator strip at the bottom of every page.
  paint_indicator(canvas, g_state.pages, g_state.page_index);
```

- [ ] **Step 4: Update viewport to reserve only the bottom strip for now**

Locate:

```cpp
  Rect vp{0, 0, W, (int16_t)(H - IND_STRIP_H)};
```

Replace with:

```cpp
  Rect vp{0, 0, W, (int16_t)(H - BOT_STRIP_H)};
```

(Top reservation happens in Task 8 once `paint_status_bar` exists.)

- [ ] **Step 5: Build, flash, verify**

```bash
pio run -e m5stack-papercolor -t upload
pio device monitor
```
Visually verify: bottom strip is one row; current page name appears between the dots and NEXT label. Data/refresh text is gone. (Top is still the old layout — we'll fix in Task 8.)

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat(fw): slim bottom strip to single nav row with current page name"
```

### Task 8: Top status bar — `paint_status_bar`

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Move `format_local_hhmm` to live above `paint_status_bar`**

If you left `format_local_hhmm` in place in Task 7, no move needed — both
painters can use it. Otherwise restore it now.

- [ ] **Step 2: Add icon-glyph helper**

Above `paint_status_bar`, add a small lookup helper that pulls a baked
16x16 glyph from `icons_data.h` and blits it to the canvas. The existing
`primitives.cpp` has the full painter (with color); for the status bar we
just need a 1-bit blit in black. Add directly:

```cpp
static void blit_icon16(M5Canvas& canvas, int16_t x, int16_t y, const char* name) {
  using rcpaper::icons::kGlyphs;
  for (int i = 0; kGlyphs[i].name; ++i) {
    if (strcmp(kGlyphs[i].name, name) == 0) {
      const uint8_t* px = kGlyphs[i].glyph.px16;
      for (int yy = 0; yy < 16; ++yy) {
        uint16_t row = ((uint16_t)px[yy * 2] << 8) | px[yy * 2 + 1];
        for (int xx = 0; xx < 16; ++xx) {
          if (row & (0x8000 >> xx)) canvas.drawPixel(x + xx, y + yy, 0x000000);
        }
      }
      return;
    }
  }
}
```

Add the matching include near the existing render headers:

```cpp
#include "render/icons_data.h"
#include <cstring>
```

- [ ] **Step 3: Add `paint_status_bar`**

Insert this function above `paint_indicator`:

```cpp
// Top strip: battery glyph + temp + humidity · data time + refresh time.
// Painted after the page tree so the page can't clobber it. Reads from
// g_battery, g_env, and the current page's data_ts.
static void paint_status_bar(M5Canvas& canvas,
                             const PageRecord& rec,
                             const BatteryReading& batt,
                             const EnvReading& env,
                             uint64_t last_fetch_s) {
  const int16_t W = canvas.width();
  const int16_t y = 0;

  canvas.fillRect(0, y, W, TOP_STRIP_H, 0xFFFFFF);
  canvas.drawFastHLine(0, y + TOP_STRIP_H - 1, W, 0x000000);

  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(0x000000);

  int16_t x = 6;
  const int16_t y_text = y + 4;
  const int16_t y_icon = y + 3;

  // --- Battery glyph: outline 22x10 with 2x4 nipple, fill 0..18px wide. ---
  const int16_t batt_w = 22, batt_h = 10;
  const int16_t batt_y = y + (TOP_STRIP_H - batt_h) / 2;
  // Color the fill green/yellow/red.
  uint32_t fill_c = 0x1EA84B;  // green
  if (batt.pct < 30) fill_c = 0xF7C815;  // yellow
  if (batt.pct < 10) fill_c = 0xD92027;  // red
  canvas.drawRect(x, batt_y, batt_w, batt_h, 0x000000);
  canvas.fillRect(x + batt_w, batt_y + 3, 2, 4, 0x000000);
  if (batt.ok) {
    int16_t fill_w = (int16_t)((batt_w - 4) * (int)batt.pct / 100);
    if (fill_w > 0) canvas.fillRect(x + 2, batt_y + 2, fill_w, batt_h - 4, fill_c);
  }
  x += batt_w + 6;
  char num[8];
  if (batt.ok) snprintf(num, sizeof(num), "%d%%", (int)batt.pct);
  else         snprintf(num, sizeof(num), "%s", "--");
  canvas.setCursor(x, y_text);
  canvas.print(num);
  x += canvas.textWidth(num) + 8;

  // Group separator
  canvas.setCursor(x, y_text);
  canvas.print("\xC2\xB7");  // UTF-8 '·'
  x += 8;

  // --- Temperature ---
  blit_icon16(canvas, x, y_icon, "thermometer");
  x += 18;
  if (env.ok) snprintf(num, sizeof(num), "%d\xC2\xB0""C", (int)(env.temp_c + 0.5f));
  else        snprintf(num, sizeof(num), "--\xC2\xB0""C");
  canvas.setCursor(x, y_text);
  canvas.print(num);
  x += canvas.textWidth(num) + 8;

  // --- Humidity ---
  blit_icon16(canvas, x, y_icon, "droplet");
  x += 18;
  if (env.ok) snprintf(num, sizeof(num), "%d%%", (int)(env.humidity_pct + 0.5f));
  else        snprintf(num, sizeof(num), "--%%");
  canvas.setCursor(x, y_text);
  canvas.print(num);
  x += canvas.textWidth(num) + 8;

  // Group separator
  canvas.setCursor(x, y_text);
  canvas.print("\xC2\xB7");
  x += 8;

  // --- Data time (page's data_ts) ---
  blit_icon16(canvas, x, y_icon, "download");
  x += 18;
  String s = format_local_hhmm(rec.data_ts);
  canvas.setCursor(x, y_text);
  canvas.print(s.c_str());
  x += canvas.textWidth(s.c_str()) + 8;

  // --- Refresh time (device last_fetch_s) ---
  blit_icon16(canvas, x, y_icon, "refresh");
  x += 18;
  s = format_local_hhmm(last_fetch_s);
  canvas.setCursor(x, y_text);
  canvas.print(s.c_str());
}
```

- [ ] **Step 4: Reserve top viewport in `render_page`**

Locate (modified in Task 7):

```cpp
  Rect vp{0, 0, W, (int16_t)(H - BOT_STRIP_H)};
```

Replace with:

```cpp
  Rect vp{0, (int16_t)TOP_STRIP_H, W, (int16_t)(H - TOP_STRIP_H - BOT_STRIP_H)};
```

- [ ] **Step 5: Call `paint_status_bar` in `render_page`**

Locate the existing `paint_indicator(...)` call (Task 7). Directly above
it, add:

```cpp
  paint_status_bar(canvas, rec, g_battery, g_env, g_state.last_fetch_s);
```

- [ ] **Step 6: Build, flash, verify**

```bash
pio run -e m5stack-papercolor -t upload
pio device monitor
```
Visually verify (cycle pages with BtnA/BtnB):
- Top strip on every page: battery glyph + %, 🌡 T°C, 💧 H%, · 📥 data, 🔄 refresh.
- Page content begins below the top strip — no clipping.
- Bottom strip unchanged from Task 7.
- If sensors fail: numbers show `--` but icons stay.

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp
git commit -m "feat(fw): top status bar — battery + env + data/refresh times"
```

---

## Phase 5 — Producer page cleanup

### Task 9: Remove redundant brand footers from producer pages

**Files:**
- Modify: `producer/rcpaper/pages/overview.py`, `producer/rcpaper/pages/claude.py`, `producer/rcpaper/pages/codex.py`, `producer/rcpaper/pages/calendar.py`, `producer/rcpaper/pages/todo.py`
- Modify: `producer/tests/test_pages_claude.py`

- [ ] **Step 1: Update the claude footer test first**

Open `producer/tests/test_pages_claude.py` and delete the entire
`test_footer_includes_brand_and_plan` function (lines 32–43). Replace with:

```python
def test_no_brand_footer_row():
    """Brand footer moved into the firmware-painted nav strip."""
    page = build_claude_page(make_usage(), sparkline=None)
    # No child is the brand icon row anymore.
    last = page["children"][-1]
    # Last node is a content row, not the [icon('claude'), text('CLAUDE · …')] pair.
    if last["type"] == "hstack" and len(last.get("children", [])) == 2:
        first, second = last["children"]
        assert not (first.get("type") == "icon" and first.get("name") == "claude"), \
            "claude brand row should be deleted from the page footer"
```

- [ ] **Step 2: Run the test and confirm it fails**

```bash
cd producer && uv run pytest tests/test_pages_claude.py::test_no_brand_footer_row -v
```
Expected: FAIL — the brand row is still present.

- [ ] **Step 3: Strip the footer from `producer/rcpaper/pages/claude.py`**

Open the file and find the block at the end of `build_claude_page`:

```python
    # Flex spacer pushes the footer to the bottom of whatever room is left after
    # the firmware indicator strip is subtracted from the page viewport.
    # The firmware draws the page-rotation indicator strip + data/refresh
    # timestamps in the bottom band; we just place the brand + plan footer
    # above that strip.
    children.append(spacer())
    children.append(divider(color="black", thickness=1))
    children.append(
        hstack(
            align="center",
            gap=6,
            children=[
                icon("claude", size=16, color="red"),
                text(f"CLAUDE  ·  {usage.plan}", size="sm", weight="bold"),
            ],
        ),
    )

    return vstack(pad=14, gap=6, children=children)
```

Replace with:

```python
    return vstack(pad=14, gap=6, children=children)
```

Also drop the now-unused `icon` import if `icon` is no longer referenced
inside the file — ruff will flag it. Run `ruff check --fix
producer/rcpaper/pages/claude.py` afterwards.

- [ ] **Step 4: Strip the footer from `producer/rcpaper/pages/codex.py`**

Find at the end of `build_codex_page`:

```python
    # Firmware paints the page-rotation indicator + data/refresh timestamps
    # in the bottom band; the footer here just shows the brand + plan above it.
    children.append(spacer())
    children.append(divider(color="black", thickness=1))
    children.append(
        hstack(
            align="center",
            gap=6,
            children=[
                icon("codex", size=16, color="green"),
                text(f"CODEX  ·  {usage.plan}", size="sm", weight="bold"),
            ],
        ),
    )

    return vstack(pad=14, gap=6, children=children)
```

Replace with:

```python
    return vstack(pad=14, gap=6, children=children)
```

- [ ] **Step 5: Strip the footer from `producer/rcpaper/pages/calendar.py`**

Find at the end of `build_calendar_page`:

```python
    children += [
        spacer(),
        divider(color="black", thickness=2),
        spacer(size=4),
        hstack(
            align="center",
            gap=6,
            children=[
                icon("calendar", size=16, color="blue"),
                text(f"CALENDAR · {len(cal.events)} TODAY", size="sm", weight="bold"),
            ],
        ),
    ]

    return vstack(pad=18, gap=6, children=children)
```

Replace with:

```python
    return vstack(pad=18, gap=6, children=children)
```

- [ ] **Step 6: Strip the brand footer from `producer/rcpaper/pages/todo.py`** (keep the DONE progress bar — it's content, not chrome)

Find at the end of `build_todo_page`:

```python
    pct = int(100 * (1 - len(rem.items) / total))
    children += [
        spacer(),
        divider(color="black", thickness=2),
        spacer(size=4),
        hstack(
            gap=6,
            align="center",
            children=[
                text("DONE", size="sm", weight="bold", bg="green", color="white", pad=4),
                hbar(pct=pct, color="green", height=12),
                text(f"{pct}%", size="sm", weight="bold"),
            ],
        ),
        spacer(size=4),
        hstack(
            align="center",
            gap=6,
            children=[
                icon("todo", size=16, color="black"),
                text(f"TODO  ·  {rem.list_name}", size="sm", weight="bold"),
            ],
        ),
    ]
```

Replace with (drop the trailing `spacer(size=4)` and brand `hstack`):

```python
    pct = int(100 * (1 - len(rem.items) / total))
    children += [
        spacer(),
        divider(color="black", thickness=2),
        spacer(size=4),
        hstack(
            gap=6,
            align="center",
            children=[
                text("DONE", size="sm", weight="bold", bg="green", color="white", pad=4),
                hbar(pct=pct, color="green", height=12),
                text(f"{pct}%", size="sm", weight="bold"),
            ],
        ),
    ]
```

- [ ] **Step 7: Strip the footer from `producer/rcpaper/pages/overview.py`**

Find at the end of `build_overview_page` — the final `divider` + `hstack`:

```python
            spacer(),
            divider(color="black", thickness=2),
            hstack(
                align="center",
                gap=6,
                children=[
                    icon("claude", size=16, color="red"),
                    icon("codex", size=16, color="green"),
                    icon("calendar", size=16, color="blue"),
                    icon("todo", size=16, color="black"),
                    spacer(),
                    text("rcpaper · overview", size="sm", weight="bold"),
                ],
            ),
        ],
    )
```

Replace with (drop the final `divider` + `hstack`):

```python
            spacer(),
        ],
    )
```

- [ ] **Step 8: Lint and run the full producer test suite**

```bash
cd producer
uv run ruff check --fix .
uv run ruff format .
uv run pytest -v
```
Expected: all green. The renamed claude test passes; other page tests still pass because they don't assert on the deleted footer.

- [ ] **Step 9: Verify rendered preview**

```bash
cd producer
uv run preview --page overview --out /tmp/preview-overview.html
open /tmp/preview-overview.html
```
Confirm the page content ends naturally (no brand row, no `rcpaper · overview` text). The HTML preview won't show the firmware overlays — that's expected.

- [ ] **Step 10: Commit**

```bash
git add producer/rcpaper/pages/*.py producer/tests/test_pages_claude.py
git commit -m "refactor(producer): drop per-page brand footer; firmware nav strip carries it"
```

---

## Final verification

- [ ] **Native tests pass**

```bash
pio test -e native
```
Expected: all four native test suites green (palette, parser, engine, battery, env).

- [ ] **Firmware builds and flashes**

```bash
pio run -e m5stack-papercolor -t upload
pio device monitor
```

- [ ] **Manual visual sweep**

Cycle pages with BtnA/BtnB and BtnC (force refresh). For each page
(overview, claude, codex, calendar, todo, quote, image) verify:
1. Top strip shows battery glyph + %, 🌡 + temp, 💧 + humidity, separator, 📥 + data time, 🔄 + refresh time.
2. Battery fill color tracks % (green ≥ 30, yellow ≥ 10, red < 10).
3. Page content sits cleanly between top and bottom strips.
4. Bottom strip shows `◀ PREV ●○○○ CURRENT NEXT ▶`.
5. Force-refresh updates the refresh time.

- [ ] **Producer tests pass**

```bash
cd producer && uv run pytest -v
```

- [ ] **Commit any final polish, then push**

```bash
git push
```
