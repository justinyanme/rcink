#include "sense/battery.h"

#include <Arduino.h>

#include "display/pins_config.h"

namespace rcpaper {

static uint8_t pct_from_mv(uint16_t mv) {
  if (mv >= 4200) return 100;
  if (mv <= 3300) return 0;
  return (uint8_t)(((uint32_t)(mv - 3300) * 100) / 900);
}

BatteryReading read_battery() {
  pinMode(PIN_BAT_VOLT, ANALOG);
  uint32_t sum = 0;
  constexpr int kSamples = 8;
  for (int i = 0; i < kSamples; ++i) {
    sum += analogReadMilliVolts(PIN_BAT_VOLT) * 2;
    delay(2);
  }
  BatteryReading out;
  out.mv = (uint16_t)(sum / kSamples);
  out.ok = out.mv > 2500 && out.mv < 5000;
  out.pct = out.ok ? pct_from_mv(out.mv) : 0;
  return out;
}

}  // namespace rcpaper
