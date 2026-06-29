#include "sense/battery.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <Wire.h>

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

#include "display/pins_config.h"

namespace rcpaper {
namespace {

XPowersPMU g_pmu;
bool g_ready = false;

// Single-cell LiPo fallback curve, used only when the fuel gauge declines to
// report a percentage. 4.20 V -> 100%, 3.30 V -> 0%, linear between.
uint8_t pct_from_mv(uint16_t mv) {
  if (mv >= 4200) return 100;
  if (mv <= 3300) return 0;
  return (uint8_t)(((uint32_t)(mv - 3300) * 100) / 900);
}

}  // namespace

void battery_begin() {
  // Wire is already begun by main on I2C_SDA/I2C_SCL. We only touch battery
  // measurement — never the power rails, which boot enabled by default.
  g_ready = g_pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  if (!g_ready) {
    Serial.println("[batt] AXP2101 not found (battery shows --)");
    return;
  }
  g_pmu.enableBattDetection();
  g_pmu.enableBattVoltageMeasure();
  g_pmu.fuelGaugeControl(true, true);  // enable gauge + learning
  Serial.println("[batt] AXP2101 ready");
}

BatteryReading read_battery() {
  BatteryReading out;
  if (!g_ready) return out;  // ok=false, pct=0
  out.mv = (uint16_t)g_pmu.getBattVoltage();  // mV
  out.ok = out.mv > 2500 && out.mv < 5000;
  const int pct = g_pmu.getBatteryPercent();  // -1 when the gauge has no answer
  if (pct >= 0 && pct <= 100) out.pct = (uint8_t)pct;
  else if (out.ok) out.pct = pct_from_mv(out.mv);
  return out;
}

}  // namespace rcpaper

#endif  // ARDUINO
