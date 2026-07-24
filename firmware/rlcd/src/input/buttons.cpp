#include "input/buttons.h"

#include <Arduino.h>

#include "display/pins_config.h"

namespace rcpaper::buttons {
namespace {

constexpr uint32_t kDebounceMs = 40;

struct ButtonState {
  int pin;
  bool raw_down;
  bool stable_down;
  bool suppress_release;
  uint32_t raw_changed_ms;
};

ButtonState g_buttons[] = {
    {PIN_BUTTON_1, false, false, false, 0},  // GPIO0: BOOT
    {PIN_BUTTON_2, false, false, false, 0},  // GPIO18: KEY
};

bool raw_is_down(int pin) {
  return digitalRead(pin) == LOW;
}

}  // namespace

void init(bool consumed_key_wake) {
  const uint32_t now = millis();
  for (auto& button : g_buttons) {
    pinMode(button.pin, INPUT_PULLUP);
    const bool down = raw_is_down(button.pin);
    button.raw_down = down;
    button.stable_down = down;
    button.suppress_release =
        consumed_key_wake && button.pin == PIN_BUTTON_2 && down;
    button.raw_changed_ms = now;
  }
}

Events poll() {
  const uint32_t now = millis();
  Events events;

  for (auto& button : g_buttons) {
    const bool down = raw_is_down(button.pin);
    if (down != button.raw_down) {
      button.raw_down = down;
      button.raw_changed_ms = now;
    }
    if (down == button.stable_down ||
        now - button.raw_changed_ms < kDebounceMs) {
      continue;
    }

    button.stable_down = down;
    if (down) continue;

    events.activity = true;
    if (button.suppress_release) {
      button.suppress_release = false;
    } else if (button.pin == PIN_BUTTON_1) {
      events.boot_tap = true;
    } else {
      events.key_tap = true;
    }
  }

  return events;
}

bool any_down() {
  // BOOT is safe to poll while awake, but must never be armed as a deep-sleep
  // wake source because GPIO0 is also the download-mode strap.
  return raw_is_down(PIN_BUTTON_1) || raw_is_down(PIN_BUTTON_2);
}

}  // namespace rcpaper::buttons
