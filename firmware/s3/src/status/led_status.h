#pragma once

#include <stdint.h>

namespace rcpaper {

enum class LedState : uint8_t {
  Off,
  Launch,
  WifiConnecting,
  WifiConnected,
  WifiFailed,
  Syncing,
  SyncFailed,
  Refreshing,
  Idle,
  IdleDisconnected,
};

void led_status_init();
void led_status_show(LedState state);
void led_status_tick();
const char* led_state_name(LedState state);

// Briefly override the current pattern with a solid color on both LEDs for
// `duration_ms`. The overlay does NOT alter the active LedState — the
// heartbeat pattern resumes automatically when the overlay window expires.
// Safe to call from setup()/loop() at any time; cheap (~one RMT write).
void led_status_pulse(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms);

}  // namespace rcpaper
