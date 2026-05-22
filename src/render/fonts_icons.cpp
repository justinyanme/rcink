#include "render/fonts_icons.h"
#include "render/palette.h"

#ifdef ARDUINO
#include "render/fonts/vt323_13pt7b.h"
#include "render/fonts/vt323_16pt7b.h"
#include "render/fonts/vt323_18pt7b.h"
#include "render/fonts/vt323_24pt7b.h"
#endif

namespace rcpaper {

// Layout heights tracked alongside VT323's yAdvance so the layout engine
// reserves the right amount of vertical space per row. Pixel fonts pack
// taller ascenders/descenders than FreeSans. body + sm are baked at larger
// pt sizes than h1/h2 grew from FreeSans equivalents so dense dashboard rows
// don't read as cramped; see tools/fonts/bake.py for the source pt sizes.
int16_t font_height(TextSize size, Weight /*weight*/) {
  switch (size) {
    case TextSize::H1:   return 48;
    case TextSize::H2:   return 36;
    case TextSize::Body: return 32;
    case TextSize::Sm:
    default:             return 26;
  }
}

#ifdef ARDUINO

// VT323 is single-weight; the same baked glyph table serves both Regular and
// Bold lookups. body + sm have been bumped twice now (9→11→13pt for sm,
// 12→14→16pt for body) per UX feedback that overview text felt too small at
// the smaller sizes. h1/h2 stay at 24/18 pt — those felt right on the quote
// page.
const lgfx::IFont* font_for(TextSize size, Weight /*weight*/) {
  switch (size) {
    case TextSize::H1:   return &baked_fonts::VT323_24pt7b;
    case TextSize::H2:   return &baked_fonts::VT323_18pt7b;
    case TextSize::Body: return &baked_fonts::VT323_16pt7b;
    case TextSize::Sm:
    default:             return &baked_fonts::VT323_13pt7b;
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
