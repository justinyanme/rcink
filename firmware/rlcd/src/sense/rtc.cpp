#ifdef ARDUINO

#include "sense/rtc.h"

#include <Arduino.h>
#include <Wire.h>

namespace rcpaper {
namespace {

constexpr uint8_t kAddr = 0x51;
constexpr uint8_t kSecReg = 0x04;  // SECONDS..YEAR run 0x04..0x0A

uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// Sakamoto's algorithm: day of week (0=Sun) for a Gregorian date.
int day_of_week(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

}  // namespace

bool rtc_begin() {
  Wire.beginTransmission(kAddr);
  const bool ok = Wire.endTransmission() == 0;
  Serial.printf("[rtc] begin ok=%d\n", (int)ok);
  return ok;
}

bool rtc_get(RtcTime& o) {
  Wire.beginTransmission(kAddr);
  Wire.write(kSecReg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)kAddr, 7) != 7) return false;
  const uint8_t s = (uint8_t)Wire.read();
  const uint8_t mi = (uint8_t)Wire.read();
  const uint8_t h = (uint8_t)Wire.read();
  const uint8_t d = (uint8_t)Wire.read();
  const uint8_t w = (uint8_t)Wire.read();
  const uint8_t mo = (uint8_t)Wire.read();
  const uint8_t y = (uint8_t)Wire.read();
  if (s & 0x80) return false;  // oscillator-stop: time invalid after power loss
  o.sec = (int8_t)bcd2dec(s & 0x7F);
  o.min = (int8_t)bcd2dec(mi & 0x7F);
  o.hour = (int8_t)bcd2dec(h & 0x3F);
  o.day = (int8_t)bcd2dec(d & 0x3F);
  o.wday = (int8_t)(w & 0x07);
  o.mon = (int8_t)bcd2dec(mo & 0x1F);
  o.year = (int16_t)(2000 + bcd2dec(y));
  o.ok = true;
  return true;
}

bool rtc_set(int year, int mon, int day, int hour, int min, int sec) {
  Wire.beginTransmission(kAddr);
  Wire.write(kSecReg);
  Wire.write(dec2bcd((uint8_t)sec) & 0x7F);  // clears oscillator-stop flag
  Wire.write(dec2bcd((uint8_t)min));
  Wire.write(dec2bcd((uint8_t)hour));
  Wire.write(dec2bcd((uint8_t)day));
  Wire.write((uint8_t)day_of_week(year, mon, day));
  Wire.write(dec2bcd((uint8_t)mon));
  Wire.write(dec2bcd((uint8_t)(year % 100)));
  return Wire.endTransmission() == 0;
}

}  // namespace rcpaper

#endif  // ARDUINO
