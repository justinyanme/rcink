#pragma once
#include <cstdint>

namespace rcpaper {

static constexpr uint32_t kRgbBlack  = 0x000000;
static constexpr uint32_t kRgbWhite  = 0xFFFFFF;
static constexpr uint32_t kRgbRed    = 0xFF3333;
static constexpr uint32_t kRgbYellow = 0xFFC533;
static constexpr uint32_t kRgbGreen  = 0x30D158;
static constexpr uint32_t kRgbBlue   = 0x3B82F6;
static constexpr uint32_t kRgbMuted  = 0x8A8A8A;

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
uint16_t color_to_rgb565(Color c);

}  // namespace rcpaper
