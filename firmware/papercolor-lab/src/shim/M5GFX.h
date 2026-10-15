// Stand-in for <M5GFX.h> so the production VT323 headers
// (../papercolor/src/render/fonts) compile in the Lab without M5GFX: only the
// Adafruit GFX glyph/font layout, as M5GFX 5268353 lgfx_fonts.hpp:134-146.
#pragma once

#include <cstdint>

#ifndef PROGMEM
#define PROGMEM
#endif

namespace lgfx::v1 {

struct GFXglyph {
  uint32_t bitmapOffset;  // into GFXfont::bitmap
  uint8_t width, height;  // bitmap size in pixels
  uint8_t xAdvance;       // cursor advance
  int8_t xOffset, yOffset;  // cursor (baseline) to upper-left corner
};

struct GFXfont {
  uint8_t* bitmap;  // glyph bitmaps, MSB first, rows packed without padding
  GFXglyph* glyph;
  uint16_t first, last;
  uint8_t yAdvance;
};

}  // namespace lgfx::v1
