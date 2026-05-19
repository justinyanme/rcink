// src/sense/env.cpp
#include "sense/env.h"

#ifdef ARDUINO
#include <M5Unified.h>
#include <Arduino.h>
#endif

namespace rcpaper {

uint8_t sht_crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

void sht_convert_raw(uint16_t raw_t, uint16_t raw_h, float& t_c, float& rh_pct) {
  t_c = -45.0f + 175.0f * (float)raw_t / 65535.0f;
  float rh = -6.0f + 125.0f * (float)raw_h / 65535.0f;
  if (rh < 0.0f) rh = 0.0f;
  if (rh > 100.0f) rh = 100.0f;
  rh_pct = rh;
}

#ifdef ARDUINO

static constexpr uint8_t  kSht40Addr       = 0x44;
static constexpr uint8_t  kSht40CmdHighRes = 0xFD;  // T+RH, high precision

EnvReading read_sht40() {
  EnvReading r{0.0f, 0.0f, false};
  constexpr uint32_t kFreqHz = 100000;
  for (int attempt = 0; attempt < 3; ++attempt) {
    // SHT40 has no register concept — send a single command byte via start/write/stop.
    if (!M5.In_I2C.start(kSht40Addr, false, kFreqHz)
        || !M5.In_I2C.write(kSht40CmdHighRes)
        || !M5.In_I2C.stop()) {
      delay(5);
      continue;
    }
    delay(12);  // High-precision conversion is ~8.2 ms per datasheet.
    uint8_t buf[6] = {0};
    if (!M5.In_I2C.start(kSht40Addr, true, kFreqHz)
        || !M5.In_I2C.read(buf, sizeof(buf), true)
        || !M5.In_I2C.stop()) {
      delay(5);
      continue;
    }
    if (sht_crc8(&buf[0], 2) != buf[2] || sht_crc8(&buf[3], 2) != buf[5]) {
      delay(5);
      continue;
    }
    uint16_t raw_t = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t raw_h = ((uint16_t)buf[3] << 8) | buf[4];
    sht_convert_raw(raw_t, raw_h, r.temp_c, r.humidity_pct);
    r.ok = true;
    return r;
  }
  return r;
}

#endif  // ARDUINO

}  // namespace rcpaper
