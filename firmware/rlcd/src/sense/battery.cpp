#include "sense/battery.h"

#include <Arduino.h>

#include "display/pins_config.h"

namespace rcpaper {

// Waveshare ESP32-S3-RLCD-4.2 18650 calibration (from the demo's adc_bsp.cpp):
// 3.0 V -> 0%, 4.12 V -> 100%, linear between.
static uint8_t pct_from_mv(uint16_t mv) {
  if (mv >= 4120) return 100;
  if (mv <= 3000) return 0;
  return (uint8_t)(((uint32_t)(mv - 3000) * 100) / 1120);
}

BatteryReading read_battery() {
  pinMode(PIN_BAT_VOLT, ANALOG);
  uint32_t sum = 0;
  constexpr int kSamples = 8;
  for (int i = 0; i < kSamples; ++i) {
    // 3:1 resistor divider on this board (vs. the AMOLED's 2:1).
    sum += analogReadMilliVolts(PIN_BAT_VOLT) * 3;
    delay(2);
  }
  BatteryReading out;
  out.mv = (uint16_t)(sum / kSamples);
  out.ok = out.mv > 2500 && out.mv < 5000;
  out.pct = out.ok ? pct_from_mv(out.mv) : 0;
  return out;
}

}  // namespace rcpaper
