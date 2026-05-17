#pragma once
#include <M5GFX.h>
#include "proto/schema.h"

namespace rcpaper {

// Returns a font pointer the M5GFX `setFont()` accepts.
const lgfx::IFont* font_for(TextSize size, Weight weight);

// Pixel height of the chosen font (M5GFX `fontHeight()`-equivalent).
int16_t font_height(TextSize size, Weight weight);

// Draws a placeholder icon: a filled square of `name`-derived size + colour.
// Returns the size drawn.
struct IconResult { int16_t w; int16_t h; };
IconResult draw_icon_placeholder(M5GFX& gfx, int16_t x, int16_t y,
                                 const IconNode& n);

}  // namespace rcpaper
