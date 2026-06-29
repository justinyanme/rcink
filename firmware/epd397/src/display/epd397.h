#pragma once
#include <cstddef>
#include <cstdint>

namespace rcpaper {

// Driver for the Waveshare ESP32-S3-ePaper-3.97 panel (SSD1677-family, 800x480,
// 4-level grayscale). The render stack draws into a 16-bit TFT_eSprite as usual;
// this driver quantises that sprite to the panel's native 2bpp gray buffer
// (render/gray_pack.h) and drives the 4-gray waveform.

// GPIO + SPI bring-up, full init, then a white clear to wipe factory ghosting.
void epd397_init();

// Full white refresh (clears ghosting).
void epd397_clear();

// Quantise + 4-gray refresh a `w`x`h` RGB565 sprite (must be 800x480).
void epd397_push(const uint16_t* rgb565, int w, int h);

// Last packed 2bpp gray buffer (96000 bytes) sent to the panel, for the
// screenshot oracle. Callers must treat it as read-only.
const uint8_t* epd397_last_frame(size_t* len);

// Deep sleep — the panel retains the last image with no power.
void epd397_sleep();

}  // namespace rcpaper
