// src/sense/env.h
#pragma once
#include <cstdint>
#include <cstddef>

namespace rcpaper {

struct EnvReading {
  float temp_c;       // valid when ok == true
  float humidity_pct; // valid when ok == true; 0..100
  bool  ok;
};

// CRC-8 (poly 0x31, init 0xFF, no reflect, no final xor) per SHT4x spec.
// Exposed for unit tests.
uint8_t sht_crc8(const uint8_t* data, size_t len);

// Map raw 16-bit ticks from the SHT40 read response to physical units.
// Datasheet:
//   t_c          = -45 + 175 * raw_t / 65535
//   humidity_pct = -6 + 125 * raw_h / 65535, clamped to [0, 100]
// Exposed for unit tests.
void sht_convert_raw(uint16_t raw_t, uint16_t raw_h, float& t_c, float& rh_pct);

#ifdef ARDUINO
// Device-only: single-shot high-precision read at I2C 0x44 (cmd 0xFD).
// Returns ok=false on NACK, CRC fail, or timeout (3 in-place retries).
EnvReading read_sht40();
#endif

}  // namespace rcpaper
