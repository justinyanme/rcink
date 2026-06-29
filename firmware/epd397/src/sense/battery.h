#pragma once

#include <cstdint>

namespace rcpaper {

struct BatteryReading {
  bool ok = false;
  uint16_t mv = 0;
  uint8_t pct = 0;
};

// Bring up the AXP2101 PMIC fuel gauge (battery-only; rails are left untouched).
// Safe to call after Wire.begin(). No-op / degrades to ok=false if absent.
void battery_begin();

BatteryReading read_battery();

}  // namespace rcpaper
