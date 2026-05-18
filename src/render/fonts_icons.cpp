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

// efontCN_* is the M5GFX-bundled bitmap CJK font (u8g2 format). It covers
// CJK plus ASCII, so we can swap to it whenever a string contains any
// non-ASCII byte without losing Latin glyphs.
const lgfx::IFont* cjk_font_for(TextSize size, Weight weight) {
  switch (size) {
    case TextSize::H1:
      return weight == Weight::Bold ? &fonts::efontCN_24_b : &fonts::efontCN_24;
    case TextSize::H2:
      return weight == Weight::Bold ? &fonts::efontCN_24_b : &fonts::efontCN_24;
    case TextSize::Body:
      return weight == Weight::Bold ? &fonts::efontCN_16_b : &fonts::efontCN_16;
    case TextSize::Sm:
    default:
      return weight == Weight::Bold ? &fonts::efontCN_14_b : &fonts::efontCN_14;
  }
}

bool string_needs_cjk(const std::string& s) {
  // efontCN ships with ASCII too, but switching unnecessarily makes Latin
  // headers heavier — only swap when there's at least one non-ASCII byte.
  for (unsigned char c : s) {
    if (c >= 0x80) return true;
  }
  return false;
}

IconResult draw_icon_placeholder(M5GFX& gfx, int16_t x, int16_t y,
                                 const IconNode& n) {
  int16_t s = n.size > 0 ? n.size : 16;
  gfx.fillRect(x, y, s, s, color_to_m5gfx(n.color));
  return { s, s };
}

#endif  // ARDUINO

}  // namespace rcpaper
