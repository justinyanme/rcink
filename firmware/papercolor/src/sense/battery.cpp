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
static_assert(sizeof(kAnchorMv) / sizeof(kAnchorMv[0]) == kN,
              "kAnchorMv and kAnchorPct must have the same length");
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
  constexpr uint8_t  kAddr   = 0x6E;
  constexpr uint32_t kFreqHz = 100000;
  // PM1 needs both VBAT bytes (0x22 lo, 0x23 hi) read in a single
  // [start][write addr][write 0x22][restart][read 2 bytes][stop] transaction;
  // splitting it into two single-byte reads returns 0 on this chip.
  uint8_t buf[2] = {0, 0};
  if (!M5.In_I2C.readRegister(kAddr, 0x22, buf, 2, kFreqHz)) return r;
  uint16_t mv = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  r.mv  = mv;
  r.pct = battery_mv_to_pct(mv);
  r.ok  = true;
  return r;
}
#endif

}  // namespace rcpaper
