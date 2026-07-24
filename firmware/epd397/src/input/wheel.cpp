#include "input/wheel.h"

#include <Arduino.h>

#include "display/pins_config.h"

namespace rcpaper {
namespace {

// The active-low 3-way wheel is GPIO4/GPIO5/GPIO6. A release after 40 ms is a
// tap; holding any position for 800 ms emits one long-press event.
struct Button {
  int pin;
  bool pressed;
  uint32_t press_ms;
  bool long_fired;
};

Button g_buttons[] = {
    {PIN_BUTTON_UP, false, 0, false},
    {PIN_BUTTON_2, false, 0, false},
    {PIN_BUTTON_DOWN, false, 0, false},
};

WheelHandler g_tap_handler = nullptr;
WheelHandler g_long_press_handler = nullptr;

constexpr uint32_t kLongPressMs = 800;
constexpr uint32_t kDebounceMs = 40;

}  // namespace

void wheel_prepare_wake_inputs() {
  pinMode(PIN_BUTTON_UP, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);
  pinMode(PIN_BUTTON_DOWN, INPUT_PULLUP);
}

uint64_t wheel_wake_mask() {
  return (1ULL << PIN_BUTTON_UP) | (1ULL << PIN_BUTTON_2) |
         (1ULL << PIN_BUTTON_DOWN);
}

int wheel_wake_pin(bool ext1_wake, uint64_t ext1_mask) {
  if (!ext1_wake) return -1;
  if (ext1_mask & (1ULL << PIN_BUTTON_UP)) return PIN_BUTTON_UP;
  if (ext1_mask & (1ULL << PIN_BUTTON_2)) return PIN_BUTTON_2;
  if (ext1_mask & (1ULL << PIN_BUTTON_DOWN)) return PIN_BUTTON_DOWN;
  return -1;
}

void wheel_init(WheelHandler tap_handler, WheelHandler long_press_handler) {
  if (!tap_handler || !long_press_handler) {
    Serial.println("[wheel] FATAL missing event handler");
    abort();
  }
  g_tap_handler = tap_handler;
  g_long_press_handler = long_press_handler;
  wheel_prepare_wake_inputs();
  for (auto& button : g_buttons) {
    button.pressed = digitalRead(button.pin) == LOW;
    button.press_ms = millis();
    button.long_fired = false;
  }
}

void wheel_poll() {
  const uint32_t now = millis();
  for (auto& button : g_buttons) {
    const bool down = digitalRead(button.pin) == LOW;
    if (down && !button.pressed) {
      button.pressed = true;
      button.press_ms = now;
      button.long_fired = false;
    } else if (down && button.pressed) {
      if (!button.long_fired &&
          now - button.press_ms >= kLongPressMs) {
        button.long_fired = true;
        g_long_press_handler(button.pin);
      }
    } else if (!down && button.pressed) {
      const uint32_t held_ms = now - button.press_ms;
      button.pressed = false;
      if (!button.long_fired && held_ms >= kDebounceMs) {
        g_tap_handler(button.pin);
      }
    }
  }
}

}  // namespace rcpaper
