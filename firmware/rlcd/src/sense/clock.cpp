#include "sense/clock.h"

#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

#include "sense/rtc.h"

namespace rcpaper {

bool wall_clock_is_valid() {
  return time(nullptr) >=
         1704067200;  // 2024-01-01 UTC; earlier means RTC/NTP absent.
}

void seed_clock_from_rtc() {
  RtcTime rt;
  if (!rtc_get(rt) || !rt.ok || rt.year < 2024) {
    Serial.println("[rtc] no valid time (needs NTP sync)");
    return;
  }
  struct tm tmv = {};
  tmv.tm_year = rt.year - 1900;
  tmv.tm_mon = rt.mon - 1;
  tmv.tm_mday = rt.day;
  tmv.tm_hour = rt.hour;
  tmv.tm_min = rt.min;
  tmv.tm_sec = rt.sec;
  tmv.tm_isdst = -1;
  const time_t epoch = mktime(&tmv);
  const struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
  settimeofday(&tv, nullptr);
  Serial.printf("[rtc] clock seeded %04d-%02d-%02d %02d:%02d:%02d\n",
                rt.year, rt.mon, rt.day, rt.hour, rt.min, rt.sec);
}

void write_rtc_from_clock() {
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  if (rtc_set(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour,
              ti.tm_min, ti.tm_sec)) {
    Serial.println("[rtc] written from NTP-synced clock");
  }
}

}  // namespace rcpaper
