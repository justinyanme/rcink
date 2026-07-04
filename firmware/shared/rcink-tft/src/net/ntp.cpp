#ifdef ARDUINO

#include "net/ntp.h"

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <esp_sntp.h>
#if ESP_ARDUINO_VERSION_MAJOR < 3
#include <lwip/apps/sntp.h>
#endif
#include <time.h>

namespace rcpaper {

void apply_local_tz(const char* tz) {
  setenv("TZ", tz, 1);
  tzset();
}

static void stop_sntp() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_sntp_stop();
#else
  sntp_stop();
#endif
}

bool ntp_sync_once(const char* tz) {
  configTime(0, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
  apply_local_tz(tz);
  const uint32_t start = millis();
  bool ok = false;
  while (millis() - start <= 10000) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      ok = true;
      break;
    }
    delay(100);
  }
  // Keep SNTP from retrying its UDP socket while DNS/TLS fetches run next.
  stop_sntp();
  if (!ok) {
    Serial.println("[ntp] sync timeout");
    return false;
  }

  time_t now;
  struct tm ti;
  time(&now);
  localtime_r(&now, &ti);
  Serial.printf("[ntp] %04d-%02d-%02d %02d:%02d:%02d tz=%s\n",
                ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                ti.tm_hour, ti.tm_min, ti.tm_sec, tz);
  return true;
}

}  // namespace rcpaper

#endif  // ARDUINO
