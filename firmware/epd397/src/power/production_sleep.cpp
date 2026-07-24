#include "power/production_sleep.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <time.h>

#include "input/wheel.h"
#include "net/wifi.h"
#include "power/sleep.h"
#include "render/page.h"

namespace rcpaper {
namespace {

uint64_t next_minute_epoch() {
  const uint64_t now = static_cast<uint64_t>(time(nullptr));
  return ((now / 60) + 1) * 60;
}

}  // namespace

[[noreturn]] void enter_production_sleep() {
  if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
  display_sleep();
  wheel_prepare_wake_inputs();
  esp_sleep_enable_ext1_wakeup(wheel_wake_mask(),
                               ESP_EXT1_WAKEUP_ANY_LOW);
  deep_sleep_until(next_minute_epoch());
}

}  // namespace rcpaper
