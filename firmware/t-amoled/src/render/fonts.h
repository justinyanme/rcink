#pragma once

#include "proto/schema.h"
#include "render/engine.h"

#ifdef ARDUINO
#include <TFT_eSPI.h>
#endif

namespace rcpaper {

int16_t font_height(TextSize size, Weight weight);

#ifdef ARDUINO
void select_font(TFT_eSprite& canvas, TextSize size, Weight weight);
int16_t text_width(TFT_eSprite& canvas, const TextNode& text, const char* value);
std::string printable_text(const std::string& in);
#endif

}  // namespace rcpaper
