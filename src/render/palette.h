#pragma once
#include <cstdint>

namespace rcpaper {

static constexpr uint32_t kE6Black  = 0x000000;
static constexpr uint32_t kE6White  = 0xFFFFFF;
static constexpr uint32_t kE6Red    = 0xFF0000;
static constexpr uint32_t kE6Yellow = 0xFFFF00;
static constexpr uint32_t kE6Green  = 0x00FF00;
static constexpr uint32_t kE6Blue   = 0x0000FF;

enum class Color : uint8_t {
  Black  = 0,
  White  = 1,
  Red    = 2,
  Yellow = 3,
  Green  = 4,
  Blue   = 5,
};

// Pure: maps a protocol token to an E6 ink. Case-sensitive.
// Returns false for null, empty, or non-palette tokens.
bool color_from_token(const char* token, Color* out);

const char* color_to_token(Color c);
uint32_t color_to_rgb(Color c);
uint32_t quantize_rgb_to_e6(uint8_t r, uint8_t g, uint8_t b);

#ifdef ARDUINO
// Device-side: maps Color enum to a solid E6 RGB anchor for M5GFX.
// Defined only in the Arduino build.
uint32_t color_to_m5gfx(Color c);
#endif

}  // namespace rcpaper
