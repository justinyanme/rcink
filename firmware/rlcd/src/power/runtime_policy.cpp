#include "power/runtime_policy.h"

namespace rcpaper {

RuntimeState initial_runtime_state(const RuntimeBootInput& input) {
  if (input.usb_host_present) {
    return {RuntimeMode::Host, 0, 0, false};
  }
  if (input.always_active || input.key_wake) {
    return {
        RuntimeMode::Active,
        input.now_ms + kActiveWindowMs,
        0,
        false,
    };
  }
  return {RuntimeMode::Eco, 0, 0, false};
}

RuntimeDecision advance_runtime_state(RuntimeState state,
                                      const RuntimeInput& input) {
  const RuntimeMode previous = state.mode;
  if (input.usb_host_present) {
    state = {RuntimeMode::Host, 0, 0, false};
  } else if (state.mode == RuntimeMode::Host) {
    if (!state.host_missing) {
      state.host_missing = true;
      state.host_missing_since_ms = input.now_ms;
    } else if (input.now_ms - state.host_missing_since_ms >=
               kHostLossDebounceMs) {
      state = {RuntimeMode::Active, input.now_ms + kActiveWindowMs, 0, false};
    }
  } else if (input.always_active ||
             (state.mode == RuntimeMode::Eco && input.user_activity)) {
    state = {RuntimeMode::Active, input.now_ms + kActiveWindowMs, 0, false};
  } else if (state.mode == RuntimeMode::Active) {
    if (input.user_activity || input.any_button_down) {
      state.active_deadline_ms = input.now_ms + kActiveWindowMs;
    } else if (static_cast<int32_t>(input.now_ms -
                                    state.active_deadline_ms) >= 0) {
      state = {RuntimeMode::Eco, 0, 0, false};
    }
  }
  const bool mode_changed = state.mode != previous;
  return {state, mode_changed,
          state.mode == RuntimeMode::Eco && !input.any_button_down &&
              !input.always_active && !mode_changed};
}

const char* runtime_mode_name(RuntimeMode mode) {
  switch (mode) {
    case RuntimeMode::Eco:
      return "ECO";
    case RuntimeMode::Active:
      return "ACTIVE";
    case RuntimeMode::Host:
      return "HOST";
  }
  return "INVALID";
}

}  // namespace rcpaper
