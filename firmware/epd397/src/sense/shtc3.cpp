#ifdef ARDUINO

#include "sense/shtc3.h"

#include <Arduino.h>
#include <Wire.h>

namespace rcpaper {
namespace {

constexpr uint8_t kAddr = 0x70;
constexpr uint16_t kWakeup = 0x3517;
constexpr uint16_t kSleep = 0xB098;
constexpr uint16_t kSoftReset = 0x805D;
// Measure T first, normal mode, clock-stretching disabled.
constexpr uint16_t kMeasTRhPolling = 0x7866;

bool write_cmd(uint16_t c) {
  Wire.beginTransmission(kAddr);
  Wire.write((uint8_t)(c >> 8));
  Wire.write((uint8_t)(c & 0xFF));
  return Wire.endTransmission() == 0;
}

// Sensirion CRC-8: poly 0x31, init 0xFF, MSB-first, over the 2 data bytes.
uint8_t crc8(uint8_t msb, uint8_t lsb) {
  uint8_t crc = 0xFF;
  const uint8_t bytes[2] = {msb, lsb};
  for (uint8_t b : bytes) {
    crc ^= b;
    for (int i = 0; i < 8; ++i) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

}  // namespace

bool shtc3_begin() {
  write_cmd(kWakeup);
  delay(1);
  const bool ok = write_cmd(kSoftReset);
  delay(1);
  write_cmd(kSleep);
  Serial.printf("[shtc3] begin ok=%d\n", (int)ok);
  return ok;
}

Shtc3Reading shtc3_read() {
  Shtc3Reading out;
  write_cmd(kWakeup);
  delayMicroseconds(300);  // tWAKEUP
  if (!write_cmd(kMeasTRhPolling)) {
    write_cmd(kSleep);
    return out;
  }
  delay(20);  // > 12.1 ms max conversion
  if (Wire.requestFrom((int)kAddr, 6) != 6) {
    write_cmd(kSleep);
    return out;
  }
  uint8_t b[6];
  for (uint8_t& x : b) x = (uint8_t)Wire.read();
  write_cmd(kSleep);

  if (crc8(b[0], b[1]) != b[2] || crc8(b[3], b[4]) != b[5]) return out;

  const uint16_t traw = (uint16_t)((b[0] << 8) | b[1]);
  const uint16_t hraw = (uint16_t)((b[3] << 8) | b[4]);
  out.temp_c = -45.0f + 175.0f * (float)traw / 65536.0f + SHTC3_TEMP_OFFSET_C;
  out.rh = 100.0f * (float)hraw / 65536.0f;
  if (out.rh < 0.0f) out.rh = 0.0f;
  if (out.rh > 100.0f) out.rh = 100.0f;
  out.ok = true;
  return out;
}

}  // namespace rcpaper

#endif  // ARDUINO
