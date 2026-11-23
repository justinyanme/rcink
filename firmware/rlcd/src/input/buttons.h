#pragma once

namespace rcpaper::buttons {

struct Events {
  // Any debounced press/release or long-press edge. Runtime uses this to keep
  // the ACTIVE window alive.
  bool activity = false;
  bool boot_tap = false;
  bool key_tap = false;
  bool boot_long = false;
  bool key_long = false;
};

// GPIO18/KEY can wake deep sleep. Pass true when that wake press has already
// advanced navigation so its release does not trigger a tap or long press.
void init(bool consumed_key_wake);

// Polls button states, firing tap on release (< 600 ms) or long_press at 600 ms.
Events poll();

// Immediate active-low GPIO state for preventing sleep while either is held.
bool any_down();

}  // namespace rcpaper::buttons
