#pragma once

#include "proto/schema.h"
#include "render/engine.h"

#ifdef ARDUINO
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>
#endif

namespace rcpaper {

int16_t font_height(TextSize size, Weight weight);

#ifdef ARDUINO

// Latin text uses TFT_eSPI's built-in vector fonts (crisp, scalable). Any
// string containing a non-ASCII byte is routed through U8g2_for_TFT_eSPI with a
// baked WenQuanYi 16px GB2312 bitmap font — TFT_eSPI ships no CJK glyphs. This
// mirrors firmware/s3's efontCN swap (see render/fonts_icons.cpp there).

// Reserved row height for CJK glyphs (the baked font is 16px tall).
static constexpr int16_t kCjkLineH = 16;

// --- fit-to-height scaling ---------------------------------------------------
// render_page measures a page's natural height and, if it overflows the
// viewport, re-lays-out at a reduced scale so everything fits on one screen.
// The scale shrinks Latin text down the discrete TFT font ladder, CJK to its
// 16px floor, and (via the engine) all pads/gaps/spacers/element heights.

// Set the active text scale (1.0 = nominal). Affects select_font, measured_width
// and scale_px until changed. Reset to 1.0 after each page.
void set_text_scale(float s);

// Round `v` by the active text scale (used for per-node text padding).
int16_t scale_px(int16_t v);

// Rendered line height of `size` at the active scale (what the font ladder
// actually picks), for the layout measurer.
int16_t text_line_height(TextSize size);

// Bind the u8g2 renderer to the off-screen sprite. Call once after the sprite
// is created. U8g2_for_TFT_eSPI draws via the virtual drawFast{H,V}Line, which
// TFT_eSprite overrides, so glyphs land in the sprite buffer like everything
// else and get pushed in one shot.
void fonts_begin(TFT_eSprite& canvas);

// True when `s` has any byte >= 0x80 (i.e. needs the CJK font for fidelity).
bool string_needs_cjk(const std::string& s);

// Rendered width of `s` at (size, weight), CJK-aware, excluding padding.
int16_t measured_width(TFT_eSprite& canvas, TextSize size, Weight weight,
                       const std::string& s);

// Draw `s` with its top-left at (x, y_top), CJK-aware. `opaque` paints `bg`
// behind the glyphs (Latin path only; CJK draws transparent over a pre-filled
// rect). Latin glyphs outside the built-in fonts are sanitised to '?'.
void draw_utf8(TFT_eSprite& canvas, int16_t x, int16_t y_top, TextSize size,
               Weight weight, uint16_t fg, bool opaque, uint16_t bg,
               const std::string& s);

// Sanitise multi-byte UTF-8 to '?' for the ASCII-only built-in fonts. Used by
// the status bar / nav strip, where the page id is always ASCII anyway.
std::string printable_text(const std::string& in);
#endif

}  // namespace rcpaper
