#include "render/fonts_icons.h"
#include "render/palette.h"

namespace rcpaper {

int16_t font_height(TextSize size, Weight /*weight*/) {
  switch (size) {
    case TextSize::H1:   return 36;
    case TextSize::H2:   return 28;
    case TextSize::Body: return 18;
    case TextSize::Sm:
    default:             return 14;
  }
}

#ifdef ARDUINO

// Use M5GFX's built-in scalable fonts (no asset baking required for v1).
// FreeSans is bundled; bold variants where available.
const lgfx::IFont* font_for(TextSize size, Weight weight) {
  switch (size) {
    case TextSize::H1:
      return weight == Weight::Bold ? &fonts::FreeSansBold24pt7b
                                    : &fonts::FreeSans24pt7b;
    case TextSize::H2:
      return weight == Weight::Bold ? &fonts::FreeSansBold18pt7b
                                    : &fonts::FreeSans18pt7b;
    case TextSize::Body:
      return weight == Weight::Bold ? &fonts::FreeSansBold12pt7b
                                    : &fonts::FreeSans12pt7b;
    case TextSize::Sm:
    default:
      return weight == Weight::Bold ? &fonts::FreeSansBold9pt7b
                                    : &fonts::FreeSans9pt7b;
  }
}

IconResult draw_icon_placeholder(M5GFX& gfx, int16_t x, int16_t y,
                                 const IconNode& n) {
  int16_t s = n.size > 0 ? n.size : 16;
  gfx.fillRect(x, y, s, s, color_to_m5gfx(n.color));
  return { s, s };
}

#endif  // ARDUINO

}  // namespace rcpaper
