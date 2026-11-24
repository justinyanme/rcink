#include "input/buttons.h"

#include <Arduino.h>

#include "display/pins_config.h"
#include "input/button_gesture.h"

namespace rcpaper::buttons {
namespace {

DualButtonGestureTracker g_tracker;

bool raw_is_down(int pin) {
  return digitalRead(pin) == LOW;
}

}  // namespace

WakeKeyDisposition init(bool key_wake) {
  const uint32_t now = millis();
  pinMode(PIN_BUTTON_1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);
  const bool boot_down = raw_is_down(PIN_BUTTON_1);
  const bool key_down = raw_is_down(PIN_BUTTON_2);
  const WakeKeyDisposition disp = classify_key_wake(key_wake, key_down);
  const uint32_t key_pre_elapsed =
      disp == WakeKeyDisposition::TrackHold ? kWakePressPreElapsedMs : 0;
  g_tracker.init(boot_down, key_down, false, now, key_pre_elapsed);
  return disp;
}

Events poll() {
  const uint32_t now = millis();
  const DualButtonGestureOutput out =
      g_tracker.poll(raw_is_down(PIN_BUTTON_1), raw_is_down(PIN_BUTTON_2), now);
  Events events;
  events.activity = out.activity;
  events.boot_tap = out.boot_tap;
  events.key_tap = out.key_tap;
  events.boot_long = out.boot_long;
  events.key_long = out.key_long;
  events.chord = out.chord;
  return events;
}

bool any_down() {
  // BOOT is safe to poll while awake, but must never be armed as a deep-sleep
  // wake source because GPIO0 is also the download-mode strap.
  return raw_is_down(PIN_BUTTON_1) || raw_is_down(PIN_BUTTON_2);
}

}  // namespace rcpaper::buttons
