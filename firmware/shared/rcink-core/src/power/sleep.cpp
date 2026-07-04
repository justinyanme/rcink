#ifdef ARDUINO

#include "power/sleep.h"

#include <Arduino.h>
#include <esp_sleep.h>
#include <time.h>

namespace rcpaper {

[[noreturn]] void deep_sleep_until(uint64_t epoch_seconds) {
  time_t now;
  time(&now);
  int64_t delta_raw = (int64_t)epoch_seconds - (int64_t)now;
  int64_t delta = delta_raw;
  if (delta < 10) {
    Serial.printf("[sleep] target past-due: target=%llu now=%lld delta=%lld, clamped to 10s\n",
                  (unsigned long long)epoch_seconds, (long long)now, (long long)delta_raw);
    delta = 10;
  }

  esp_sleep_enable_timer_wakeup((uint64_t)delta * 1000000ULL);
  Serial.printf("[sleep] entering deep sleep for %lld s\n", (long long)delta);
  Serial.flush();
  esp_deep_sleep_start();
  while (true) delay(1000);
}

}  // namespace rcpaper

#endif  // ARDUINO
