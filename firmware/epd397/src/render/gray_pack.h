#pragma once
#include <cstdint>

namespace rcpaper {

// The render stack composes into a 16-bit RGB565 sprite (black ink on a white
// field). The 3.97" e-paper renders 4 gray levels, so at push time every sprite
// pixel is quantised by luminance to a 2-bit code: 0 = black (darkest) … 3 =
// white (lightest). Latin antialiasing therefore survives as real gray edges.

// Luminance (0..255) of an RGB565 pixel (Rec.601 weights, 8-bit expanded).
uint8_t gray_lum565(uint16_t px);

// 4-level code (0=black..3=white) for a luminance value.
inline uint8_t gray_code_from_lum(uint8_t lum) {
  return (uint8_t)(((int)lum * 3 + 127) / 255);
}

// Pack a `w`x`h` RGB565 framebuffer (row-major, top-left origin) into the
// panel's 4-gray buffer: row-major, 4 px/byte MSB-first (first pixel in bits
// [7:6]), 200 bytes/row for w=800. `out` must be w*h/4 bytes (800x480 ->
// 96000). `w` must be a multiple of 4. This matches Waveshare GUI_Paint's
// ROTATE_0 4-gray layout, which `EPD_3IN97_Display_4Gray` consumes.
void pack_gray_landscape(const uint16_t* rgb565, int w, int h, uint8_t* out);

// 2-bit gray code (0..3) of pixel `n` (raster order) in a packed gray buffer.
inline uint8_t gray_code_at(const uint8_t* g, int n) {
  return (uint8_t)((g[n >> 2] >> (6 - ((n & 3) << 1))) & 0x3);
}

// One SSD1677 1bpp plane byte for the 8 pixels starting at `pixel8_base`
// (must be a multiple of 8), MSB = first pixel. The 4-gray waveform writes two
// planes: plane 0 -> RAM 0x24 (bit set for codes {black, light-gray}), plane 1
// -> RAM 0x26 (bit set for codes {black, dark-gray}). Derived from the vendor
// EPD_3IN97_Display_4Gray bit mapping.
uint8_t gray_plane_byte(const uint8_t* g, int pixel8_base, int plane);

// One 1bpp byte (8 pixels, MSB first) thresholding the gray buffer to black/
// white for the fast (mono, ~1.5s) refresh: white bit (1) for light codes
// {2,3}, black bit (0) for dark codes {0,1}. Antialiased edges split at the
// midpoint. Panel convention: 1 = white, 0 = black.
uint8_t gray_to_mono_byte(const uint8_t* g, int pixel8_base);

}  // namespace rcpaper
