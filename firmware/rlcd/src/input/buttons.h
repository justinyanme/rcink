#pragma once

#include "input/button_gesture.h"

namespace rcpaper::buttons {

struct Events {
  // Any debounced press/release, long-press, or chord edge. Runtime uses this
  // to keep the ACTIVE window alive.
  bool activity = false;
  bool boot_tap = false;
  bool key_tap = false;
  bool boot_long = false;
  bool key_long = false;
  bool chord = false;
};

// GPIO18/KEY can wake deep sleep. Classifies the wake press at setup():
// returns ImmediateTap if KEY is already released, or TrackHold if still held
// low so the main loop can distinguish a tap (< 600 ms) from a hold (>= 600 ms).
WakeKeyDisposition init(bool key_wake);

// Polls button states, firing tap on release (< 600 ms), long_press at 600 ms,
// or chord when both KEY and BOOT become debounced-down simultaneously.
Events poll();

// Immediate active-low GPIO state for preventing sleep while either is held.
bool any_down();

}  // namespace rcpaper::buttons
