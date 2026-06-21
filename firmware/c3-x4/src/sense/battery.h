#pragma once

#include <cstdint>

namespace rcpaper {

struct BatteryReading {
  bool ok = false;
  uint16_t mv = 0;
  uint8_t pct = 0;
};

BatteryReading read_battery();

}  // namespace rcpaper
