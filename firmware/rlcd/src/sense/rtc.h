#pragma once
#include <cstdint>

namespace rcpaper {

// PCF85063 real-time clock (I2C 0x51). Keeps wall-clock time across reboots
// (and across power loss if an RTC backup cell is fitted), so the dashboard
// shows a correct clock offline. NTP, when Wi-Fi is up, re-syncs it.
struct RtcTime {
  int16_t year = 0;  // full year, e.g. 2026
  int8_t mon = 0;    // 1..12
  int8_t day = 0;    // 1..31
  int8_t hour = 0;   // 0..23
  int8_t min = 0;
  int8_t sec = 0;
  int8_t wday = 0;   // 0..6
  bool ok = false;   // false => oscillator-stop flag set / read failed
};

// Probe the PCF85063 on the (already-begun) shared I2C bus.
bool rtc_begin();

// Read the current time. ok=false if the read fails or the oscillator-stop
// flag is set (i.e. the clock lost power and the time is meaningless).
bool rtc_get(RtcTime& out);

// Set the clock. Weekday is derived from the date.
bool rtc_set(int year, int mon, int day, int hour, int min, int sec);

}  // namespace rcpaper
