#pragma once
#include <cstdint>

namespace rcpaper {

// Self-heating compensation. The SHTC3 sits next to the ESP32-S3 and reads a
// few degrees high; Waveshare's own demo subtracts a fixed 4 C. HIL on this
// unit confirmed ~+4 C of self-heat (raw 32.7 C in a ~29 C room), so we match
// Waveshare. Re-dial against a reference thermometer if a unit reads off.
static constexpr float SHTC3_TEMP_OFFSET_C = -4.0f;

struct Shtc3Reading {
  float temp_c = 0.0f;
  float rh = 0.0f;  // %RH, clamped to [0, 100]
  bool ok = false;
};

// Wake + soft-reset + verify the SHTC3 on the (already-begun) shared I2C bus.
bool shtc3_begin();

// One polling temperature+humidity measurement. Returns ok=false (and leaves
// the values at 0) on a NACK or CRC failure — fail loud, never fake a reading.
Shtc3Reading shtc3_read();

}  // namespace rcpaper
