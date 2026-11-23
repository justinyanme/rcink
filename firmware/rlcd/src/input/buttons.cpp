#include "input/buttons.h"

#include <Arduino.h>

#include "display/pins_config.h"
#include "input/button_gesture.h"

namespace rcpaper::buttons {
namespace {

ButtonGestureTracker g_boot_tracker;
ButtonGestureTracker g_key_tracker;

bool raw_is_down(int pin) {
  return digitalRead(pin) == LOW;
}

}  // namespace

void init(bool consumed_key_wake) {
  const uint32_t now = millis();
  pinMode(PIN_BUTTON_1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);
  g_boot_tracker.init(raw_is_down(PIN_BUTTON_1), false, now);
  g_key_tracker.init(raw_is_down(PIN_BUTTON_2), consumed_key_wake, now);
}

Events poll() {
  const uint32_t now = millis();
  Events events;
  const ButtonGestureOutput boot =
      g_boot_tracker.poll(raw_is_down(PIN_BUTTON_1), now);
  const ButtonGestureOutput key =
      g_key_tracker.poll(raw_is_down(PIN_BUTTON_2), now);

  events.activity = boot.activity || key.activity;
  events.boot_tap = boot.tap;
  events.boot_long = boot.long_press;
  events.key_tap = key.tap;
  events.key_long = key.long_press;
  return events;
}

bool any_down() {
  // BOOT is safe to poll while awake, but must never be armed as a deep-sleep
  // wake source because GPIO0 is also the download-mode strap.
  return raw_is_down(PIN_BUTTON_1) || raw_is_down(PIN_BUTTON_2);
}

}  // namespace rcpaper::buttons
