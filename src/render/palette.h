#pragma once
#include <cstdint>

namespace rcpaper {

enum class Color : uint8_t {
  Black  = 0,
  White  = 1,
  Red    = 2,
  Yellow = 3,
  Green  = 4,
  Blue   = 5,
};

// Pure: maps protocol token to enum. Case-sensitive. Unknown → Black.
Color color_from_token(const char* token);

#ifdef ARDUINO
// Device-side: maps Color enum to the M5GFX value the E6 panel expects.
// Defined only in the Arduino build.
uint32_t color_to_m5gfx(Color c);
#endif

}  // namespace rcpaper
