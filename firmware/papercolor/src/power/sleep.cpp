// src/power/sleep.cpp
#include "power/sleep.h"
#include <M5Unified.h>
#include "status/led_status.h"

namespace rcpaper {
namespace {

constexpr uint8_t kPm1Address = 0x6E;
constexpr uint8_t kPm1PowerConfigRegister = 0x06;
constexpr uint8_t kPm1GpioOutputRegister = 0x11;

constexpr uint8_t kPm1RgbLdoEnable = 1 << 2;
constexpr uint8_t kPm1BoostEnable = 1 << 3;
constexpr uint8_t kPm1EpdPowerEnable = 1 << 0;
constexpr uint8_t kPm1TfPowerEnable = 1 << 3;

constexpr uint8_t kSharedSpiMosiGpio = 13;
constexpr uint8_t kSharedSpiMisoGpio = 14;
constexpr uint8_t kSharedSpiClockGpio = 15;
constexpr uint32_t kPm1I2cFrequency = 100000;

void tristate_shared_spi_bus() {
  pinMode(kSharedSpiMosiGpio, INPUT);
  pinMode(kSharedSpiMisoGpio, INPUT);
  pinMode(kSharedSpiClockGpio, INPUT);
}

void cut_sleep_power_rails() {
  const bool gpio_rails_off = M5.In_I2C.bitOff(
      kPm1Address,
      kPm1GpioOutputRegister,
      kPm1EpdPowerEnable | kPm1TfPowerEnable,
      kPm1I2cFrequency);
  const bool power_rails_off = M5.In_I2C.bitOff(
      kPm1Address,
      kPm1PowerConfigRegister,
      kPm1RgbLdoEnable | kPm1BoostEnable,
      kPm1I2cFrequency);
  if (!gpio_rails_off || !power_rails_off) {
    Serial.printf(
        "[sleep] PM1 rail cut failed gpio=%d power=%d\n",
        static_cast<int>(gpio_rails_off),
        static_cast<int>(power_rails_off));
    abort();
  }
}

}  // namespace

WakeReason wake_reason_from_esp(esp_sleep_wakeup_cause_t cause, uint64_t ext1_mask) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER: return WakeReason::Timer;
    case ESP_SLEEP_WAKEUP_EXT1: return wake_reason_from_ext1_mask(ext1_mask);
    case ESP_SLEEP_WAKEUP_UNDEFINED: return WakeReason::PowerOn;
    default: return WakeReason::Unknown;
  }
}

WakeReason current_wake_reason() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  uint64_t mask = esp_sleep_get_ext1_wakeup_status();
  return wake_reason_from_esp(cause, mask);
}

[[noreturn]] void enter_deep_sleep_until(uint64_t epoch_seconds) {
  // Required order: display sleep -> shared-SPI tristate -> rail cuts ->
  // ESP32 deep sleep. The EPD and TF card share GPIO13/14/15; driving those
  // pins after either rail is cut leaks through the unpowered device's clamp
  // diodes.
  Serial.println("[sleep] display sleep -> SPI tristate -> rail cuts -> deep sleep");
  M5.Display.sleep();
  M5.Display.waitDisplay();
  led_status_prepare_sleep();
  tristate_shared_spi_bus();
  cut_sleep_power_rails();

  esp_sleep_enable_ext1_wakeup(kButtonWakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
  deep_sleep_until(epoch_seconds);
}

}  // namespace rcpaper
