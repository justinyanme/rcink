// src/sense/battery.h
#pragma once
#include <cstdint>

namespace rcpaper {

struct BatteryReading {
  uint8_t  pct;    // 0..100
  uint16_t mv;
  bool     ok;    // false on read failure (pct/mv may be 0)
};

// Pure-logic LUT mapping Li-ion cell voltage (mV) to charge percent.
// 9-point piecewise linear: 3000/3300/3500/3650/3750/3850/3950/4050/4200 mV
// ↔ 0/5/15/30/45/60/75/90/100 %. Out-of-range values clamp.
uint8_t battery_mv_to_pct(uint16_t mv);

// Reads battery voltage from M5PM1 and maps to percent. Device-only —
// only compiled under ARDUINO.
BatteryReading read_battery();

}  // namespace rcpaper
