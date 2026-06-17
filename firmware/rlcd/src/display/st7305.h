#pragma once
#include <cstdint>

namespace rcpaper {

// Thin driver for the Waveshare ESP32-S3-RLCD-4.2's ST7305 4.2" monochrome
// reflective LCD (400x300 landscape, 1bpp). The render stack draws into a
// 16-bit TFT_eSprite as usual; this driver thresholds + packs that sprite into
// the panel's native 1bpp buffer (see render/mono_pack.h) and pushes it.

void st7305_init();

bool st7305_dark_mode();
void st7305_set_dark_mode(bool enabled);

// Threshold + pack a `w`x`h` RGB565 sprite buffer and flush the full frame.
// `w`/`h` must be 400/300 (the panel's fixed landscape geometry).
void st7305_push_mono(const uint16_t* rgb565, int w, int h);

void st7305_sleep();

}  // namespace rcpaper
