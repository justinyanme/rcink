#pragma once
#include <cstdint>

namespace rcpaper {

// The render stack composes into a 16-bit RGB565 sprite (black ink on a white
// field, like the e-ink targets). The ST7305 is a 1bpp reflective panel, so at
// push time every sprite pixel is thresholded to one bit: a pixel is "ink"
// (black) unless it is exactly white. There is NO white<->black swap (that was
// the AMOLED trick) — reflective panels are read black-on-white directly.
inline bool mono_is_ink(uint16_t px) { return px != 0xFFFF; }

// Pack a `w`x`h` RGB565 framebuffer (row-major, top-left origin) into the
// ST7305 landscape 1bpp buffer. Layout (verbatim from Waveshare's RLCD-4.2
// demo InitLandscapeLUT): one byte holds 2 columns x 4 rows = 8 px, Y is
// inverted, and bytes are column-major (the x-pair is the outer stride):
//   inv_y  = h - 1 - y
//   index  = (x>>1) * (h/4) + (inv_y>>2)
//   bit    = 7 - (((inv_y & 3) << 1) | (x & 1))
// The buffer starts all-white (0xFF) and ink pixels CLEAR their bit (display
// inversion 0x21 is on). `out` must be w*h/8 bytes; `w` even, `h` a multiple
// of 4 (400x300 -> 15000 bytes).
void pack_mono_landscape(const uint16_t* rgb565, int w, int h, uint8_t* out);

}  // namespace rcpaper
