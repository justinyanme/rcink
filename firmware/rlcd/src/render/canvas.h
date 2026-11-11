#pragma once

#ifdef ARDUINO
#include <TFT_eSPI.h>

namespace rcpaper {

// The shared 400x300 RGB565 sprite for hand-drawn views. 0xFFFF is paper and
// every other value is ink (render/mono_pack.h). Null until display_init().
TFT_eSprite* display_canvas();
// Pack and flush the sprite to the ST7305.
void display_push();

}  // namespace rcpaper
#endif
