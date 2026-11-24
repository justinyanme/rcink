#pragma once

#include <cstdint>

namespace rcpaper::buttons {

constexpr uint32_t kButtonDebounceMs = 40;
constexpr uint32_t kButtonLongPressMs = 600;
// Nominal ESP32-S3 deep-sleep EXT1 wake -> setup() delay(200) + resume time so
// a continuous hold from ECO deep sleep still fires long_press near ~600 ms.
constexpr uint32_t kWakePressPreElapsedMs = 250;

enum class WakeKeyDisposition : uint8_t {
  None,          // Not an EXT1 KEY wake.
  ImmediateTap,  // EXT1 KEY wake already released by setup() -> tap at once.
  TrackHold,     // EXT1 KEY wake still held low -> classify tap vs long_press.
};

inline WakeKeyDisposition classify_key_wake(bool key_wake, bool raw_key_down) {
  if (!key_wake) return WakeKeyDisposition::None;
  return raw_key_down ? WakeKeyDisposition::TrackHold
                      : WakeKeyDisposition::ImmediateTap;
}

struct ButtonGestureOutput {
  bool activity = false;
  bool tap = false;
  bool long_press = false;
};

struct DualButtonGestureOutput {
  bool activity = false;
  bool boot_tap = false;
  bool key_tap = false;
  bool boot_long = false;
  bool key_long = false;
  bool chord = false;
};

// Debounced single-button gesture state machine (pure header for native tests).
// Holding past kButtonLongPressMs emits `long_press` immediately and suppresses
// `tap` on the subsequent release; releasing earlier emits `tap`.
class ButtonGestureTracker {
 public:
  void init(bool initial_down, bool suppress_initial_hold, uint32_t now_ms,
            uint32_t initial_held_ms = 0) {
    raw_down_ = initial_down;
    stable_down_ = initial_down;
    suppress_release_ = suppress_initial_hold && initial_down;
    long_fired_ = false;
    raw_changed_ms_ = now_ms;
    const uint32_t credited =
        initial_down && !suppress_initial_hold
            ? (initial_held_ms >= kButtonLongPressMs ? kButtonLongPressMs - 1
                                                     : initial_held_ms)
            : 0;
    pressed_ms_ = now_ms - credited;
  }

  ButtonGestureOutput poll(bool raw_down, uint32_t now_ms) {
    const bool edge = update_debounce(raw_down, now_ms);
    return evaluate(edge, now_ms);
  }

  bool update_debounce(bool raw_down, uint32_t now_ms) {
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
      }
      return true;
    }
    return false;
  }

  ButtonGestureOutput evaluate(bool debounced_edge, uint32_t now_ms) {
    ButtonGestureOutput out;
    if (debounced_edge && !stable_down_) {
      out.activity = true;
      if (suppress_release_ || long_fired_) {
        suppress_release_ = false;
        long_fired_ = false;
      } else {
        out.tap = true;
      }
      return out;
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

  bool stable_down() const { return stable_down_; }
  bool long_fired() const { return long_fired_; }
  bool suppressed() const { return suppress_release_; }
  void suppress() { suppress_release_ = true; }
  void clear_latch() {
    suppress_release_ = false;
    long_fired_ = false;
  }

 private:
  bool raw_down_ = false;
  bool stable_down_ = false;
  bool suppress_release_ = false;
  bool long_fired_ = false;
  uint32_t raw_changed_ms_ = 0;
  uint32_t pressed_ms_ = 0;
};

// Two-button coordinator adding simultaneous KEY + BOOT chord detection on top
// of single-button debounce and long-press. Once a chord fires, tap and
// long_press on both buttons stay suppressed until BOTH return stably high.
class DualButtonGestureTracker {
 public:
  void init(bool initial_boot_down, bool initial_key_down,
            bool suppress_key_initial_hold, uint32_t now_ms,
            uint32_t key_initial_held_ms = 0) {
    boot_.init(initial_boot_down, false, now_ms, 0);
    key_.init(initial_key_down, suppress_key_initial_hold, now_ms,
              key_initial_held_ms);
    chord_active_ =
        suppress_key_initial_hold && initial_boot_down && initial_key_down;
  }

  DualButtonGestureOutput poll(bool raw_boot_down, bool raw_key_down,
                               uint32_t now_ms) {
    DualButtonGestureOutput out;
    const bool boot_edge = boot_.update_debounce(raw_boot_down, now_ms);
    const bool key_edge = key_.update_debounce(raw_key_down, now_ms);

    if (chord_active_) {
      if ((boot_edge && !boot_.stable_down()) ||
          (key_edge && !key_.stable_down())) {
        out.activity = true;
      }
      if (!boot_.stable_down() && !key_.stable_down()) {
        chord_active_ = false;
        boot_.clear_latch();
        key_.clear_latch();
      }
      return out;
    }

    if (boot_.stable_down() && key_.stable_down() &&
        !boot_.long_fired() && !key_.long_fired() &&
        !boot_.suppressed() && !key_.suppressed()) {
      chord_active_ = true;
      boot_.suppress();
      key_.suppress();
      out.activity = true;
      out.chord = true;
      return out;
    }

    const ButtonGestureOutput boot = boot_.evaluate(boot_edge, now_ms);
    const ButtonGestureOutput key = key_.evaluate(key_edge, now_ms);
    out.activity = boot.activity || key.activity;
    out.boot_tap = boot.tap;
    out.boot_long = boot.long_press;
    out.key_tap = key.tap;
    out.key_long = key.long_press;
    return out;
  }

 private:
  ButtonGestureTracker boot_;
  ButtonGestureTracker key_;
  bool chord_active_ = false;
};

}  // namespace rcpaper::buttons
