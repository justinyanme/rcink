#pragma once
#include "proto/schema.h"
#ifdef ARDUINO
#include <M5GFX.h>
#endif

namespace rcpaper {

// Pixel height of the chosen font (device-agnostic lookup table).
int16_t font_height(TextSize size, Weight weight);

#ifdef ARDUINO
// Returns a font pointer the M5GFX `setFont()` accepts.
const lgfx::IFont* font_for(TextSize size, Weight weight);

// Draws a placeholder icon: a filled square of `name`-derived size + colour.
// Returns the size drawn.
struct IconResult { int16_t w; int16_t h; };
IconResult draw_icon_placeholder(M5GFX& gfx, int16_t x, int16_t y,
                                 const IconNode& n);
#endif

}  // namespace rcpaper
