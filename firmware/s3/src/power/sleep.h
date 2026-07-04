// src/power/sleep.h
#pragma once
#include <Arduino.h>
#include <esp_sleep.h>

namespace rcpaper {

enum class WakeReason : uint8_t {
  PowerOn = 0,
  Timer,
  ButtonA, ButtonB, ButtonC,
  Unknown,
};

WakeReason wake_reason_from_esp(esp_sleep_wakeup_cause_t cause, uint64_t ext1_mask);
WakeReason current_wake_reason();
[[noreturn]] void deep_sleep_until(uint64_t epoch_seconds);
[[noreturn]] void enter_deep_sleep_until(uint64_t epoch_seconds);

}  // namespace rcpaper
