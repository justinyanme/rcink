#pragma once

#include <cstdint>

namespace rcpaper {

enum class WakeDisplayAction : uint8_t {
  Wait,
  SleepWithButtonWake,
  SleepTimerOnly,
};

enum class SleepWakePolicy : uint8_t {
  TimerAndButton,
  TimerOnly,
};

struct WakeDisplayState {
  uint32_t started_ms;
  uint32_t release_since_ms;
  bool release_pending;
};

constexpr uint32_t kWakeDisplayVisibleMs = 8000;
constexpr uint32_t kWakeButtonReleaseStableMs = 40;
constexpr uint32_t kWakeButtonHeldLimitMs = 30000;

WakeDisplayState begin_wake_display(uint32_t now_ms);
WakeDisplayAction advance_wake_display(WakeDisplayState& state,
                                       uint32_t now_ms,
                                       bool wake_button_released);

}  // namespace rcpaper
