#pragma once

#include <cstdint>

namespace rcpaper::buttons {

constexpr uint32_t kButtonDebounceMs = 40;
constexpr uint32_t kButtonLongPressMs = 600;

struct ButtonGestureOutput {
  bool activity = false;
  bool tap = false;
  bool long_press = false;
};

// Debounced single-button gesture state machine (pure header for native tests).
// Holding past kButtonLongPressMs emits `long_press` immediately and suppresses
// `tap` on the subsequent release; releasing earlier emits `tap`.
class ButtonGestureTracker {
 public:
  void init(bool initial_down, bool suppress_initial_hold, uint32_t now_ms) {
    raw_down_ = initial_down;
    stable_down_ = initial_down;
    suppress_release_ = suppress_initial_hold && initial_down;
    long_fired_ = false;
    raw_changed_ms_ = now_ms;
    pressed_ms_ = now_ms;
  }

  ButtonGestureOutput poll(bool raw_down, uint32_t now_ms) {
    ButtonGestureOutput out;
    if (raw_down != raw_down_) {
      raw_down_ = raw_down;
      raw_changed_ms_ = now_ms;
    }

    if (raw_down != stable_down_ &&
        (uint32_t)(now_ms - raw_changed_ms_) >= kButtonDebounceMs) {
      stable_down_ = raw_down;
      if (stable_down_) {
        pressed_ms_ = now_ms;
        long_fired_ = false;
      } else {
        out.activity = true;
        if (suppress_release_ || long_fired_) {
          suppress_release_ = false;
          long_fired_ = false;
        } else {
          out.tap = true;
        }
        return out;
      }
    }

    if (stable_down_ && !suppress_release_ && !long_fired_ &&
        (uint32_t)(now_ms - pressed_ms_) >= kButtonLongPressMs) {
      long_fired_ = true;
      suppress_release_ = true;
      out.activity = true;
      out.long_press = true;
    }
    return out;
  }

 private:
  bool raw_down_ = false;
  bool stable_down_ = false;
  bool suppress_release_ = false;
  bool long_fired_ = false;
  uint32_t raw_changed_ms_ = 0;
  uint32_t pressed_ms_ = 0;
};

}  // namespace rcpaper::buttons
