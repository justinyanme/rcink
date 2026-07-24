#pragma once

#include <cstdint>

namespace rcpaper {

enum class RuntimeMode : uint8_t {
  Eco,
  Active,
  Host,  // USB SOF data host; deliberately not "external power".
};

struct RuntimeState {
  RuntimeMode mode;
  uint32_t active_deadline_ms;
  uint32_t host_missing_since_ms;
  bool host_missing;
};

struct RuntimeInput {
  uint32_t now_ms;
  bool always_active;
  bool usb_host_present;  // HWCDC SOF signal, not VBUS/charger state.
  bool user_activity;
  bool any_button_down;
};

struct RuntimeBootInput {
  uint32_t now_ms;
  bool always_active;
  bool usb_host_present;  // HWCDC SOF signal, not VBUS/charger state.
  bool key_wake;
};

struct RuntimeDecision {
  RuntimeState state;
  bool mode_changed;
  bool sleep_requested;
};

constexpr uint32_t kActiveWindowMs = 8000;
constexpr uint32_t kHostLossDebounceMs = 1000;

RuntimeState initial_runtime_state(const RuntimeBootInput& input);
RuntimeDecision advance_runtime_state(RuntimeState state,
                                      const RuntimeInput& input);
const char* runtime_mode_name(RuntimeMode mode);

}  // namespace rcpaper
