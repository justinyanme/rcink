#pragma once

namespace rcpaper::buttons {

struct Events {
  // Any debounced release, including a consumed KEY wake release. Runtime
  // uses this to start the full ACTIVE window from release time.
  bool activity = false;
  bool boot_tap = false;
  bool key_tap = false;
};

// GPIO18/KEY can wake deep sleep. Pass true when that wake press has already
// advanced navigation so its release is not reported as a second tap.
void init(bool consumed_key_wake);

// Returns taps only after a debounced release. Holding a button emits nothing.
Events poll();

// Immediate active-low GPIO state for preventing sleep while either is held.
bool any_down();

}  // namespace rcpaper::buttons
