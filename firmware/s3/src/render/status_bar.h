#pragma once
#include <cstdint>

namespace rcpaper {
// Top strip height in pixels. Page layout viewport starts below this.
constexpr int16_t TOP_STRIP_H = 22;
}  // namespace rcpaper

#ifdef ARDUINO
#include <M5GFX.h>

#include "net/axiom.h"
#include "sense/battery.h"
#include "sense/env.h"

namespace rcpaper {
// Paint the status bar (battery, env, data/refresh times) on `canvas`.
// Optional groups bound themselves against canvas width; the first group that
// doesn't fit logs once and aborts the chain.
void paint_status_bar(M5Canvas& canvas,
                      const PageRecord& rec,
                      const BatteryReading& batt,
                      const EnvReading& env,
                      uint64_t last_fetch_s,
                      uint8_t unchanged_fetches);
}  // namespace rcpaper
#endif
