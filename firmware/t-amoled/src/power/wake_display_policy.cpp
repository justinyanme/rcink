#include "power/wake_display_policy.h"

namespace rcpaper {

WakeDisplayState begin_wake_display(uint32_t now_ms) {
  return {now_ms, 0, false};
}

WakeDisplayAction advance_wake_display(WakeDisplayState& state,
                                       uint32_t now_ms,
                                       bool wake_button_released) {
  if (wake_button_released) {
    if (!state.release_pending) {
      state.release_pending = true;
      state.release_since_ms = now_ms;
    }
    if (now_ms - state.started_ms >= kWakeDisplayVisibleMs &&
        now_ms - state.release_since_ms >= kWakeButtonReleaseStableMs) {
      return WakeDisplayAction::SleepWithButtonWake;
    }
  } else {
    state.release_pending = false;
  }

  if (now_ms - state.started_ms >= kWakeButtonHeldLimitMs) {
    return WakeDisplayAction::SleepTimerOnly;
  }
  return WakeDisplayAction::Wait;
}

}  // namespace rcpaper
