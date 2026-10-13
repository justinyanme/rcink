#pragma once
#include <stdint.h>

namespace rcpaper {
enum class Supply { Unknown, Battery, External };

// M5PM1 datasheet v1.9, PWR_SRC (0x04): bitmap, not an enum.
// bit0=5VIN valid, bit1=5VINOUT valid, bit2=BAT valid. Sources coexist.
constexpr Supply decode_supply(uint8_t flags) {
  return (flags & 0x03) ? Supply::External
       : (flags & 0x04) ? Supply::Battery : Supply::Unknown;
}
}  // namespace rcpaper
