// src/power/sleep.cpp
#include "power/sleep.h"
#include <M5Unified.h>
#include <time.h>

// Pin mapping from the PaperColor obsidian note.
#define BTN_A_GPIO 9
#define BTN_B_GPIO 10
#define BTN_C_GPIO 1
#define BTN_WAKE_MASK ((1ULL << BTN_A_GPIO) | (1ULL << BTN_B_GPIO) | (1ULL << BTN_C_GPIO))

namespace rcpaper {

WakeReason wake_reason_from_esp(esp_sleep_wakeup_cause_t cause, uint64_t ext1_mask) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER: return WakeReason::Timer;
    case ESP_SLEEP_WAKEUP_EXT1: {
      if (ext1_mask & (1ULL << BTN_A_GPIO)) return WakeReason::ButtonA;
      if (ext1_mask & (1ULL << BTN_B_GPIO)) return WakeReason::ButtonB;
      if (ext1_mask & (1ULL << BTN_C_GPIO)) return WakeReason::ButtonC;
      return WakeReason::Unknown;
    }
    case ESP_SLEEP_WAKEUP_UNDEFINED: return WakeReason::PowerOn;
    default: return WakeReason::Unknown;
  }
}

WakeReason current_wake_reason() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  uint64_t mask = esp_sleep_get_ext1_wakeup_status();
  return wake_reason_from_esp(cause, mask);
}

void enter_deep_sleep_until(uint64_t epoch_seconds) {
  time_t now;
  time(&now);
  int64_t delta = (int64_t)epoch_seconds - (int64_t)now;
  if (delta < 10) delta = 10;  // floor at 10 s

  // Put EPD controller in deep-sleep mode (M5GFX facade, sends cmd 0x07/0xA5).
  // Then cut the EPD power rail: PM1 GPIO0 is the EPD power enable pin; drive it
  // low via M5.In_I2C (M5Unified I2C abstraction, not raw M5PM1.h). Both steps
  // are required: controller sleep first, then rail cut.
  Serial.println("[sleep] cutting EPD power");
  M5.Display.sleep();
  M5.Display.waitDisplay();
  M5.In_I2C.bitOff(0x6E, 0x11, 1 << 0, 100000);  // PM1 GPIO0 low = EPD power off

  esp_sleep_enable_timer_wakeup((uint64_t)delta * 1000000ULL);
  esp_sleep_enable_ext1_wakeup(BTN_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);

  Serial.printf("[sleep] entering deep sleep for %lld s\n", (long long)delta);
  Serial.flush();
  esp_deep_sleep_start();
}

}  // namespace rcpaper
