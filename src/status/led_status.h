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

}  // namespace rcpaper
