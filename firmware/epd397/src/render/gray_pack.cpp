#include "render/gray_pack.h"

#include <cstring>

namespace rcpaper {

uint8_t gray_lum565(uint16_t px) {
  const int r5 = (px >> 11) & 0x1F;
  const int g6 = (px >> 5) & 0x3F;
  const int b5 = px & 0x1F;
  // Expand to full 8-bit range so pure white maps to 255 (code 3) exactly.
  const int r = (r5 << 3) | (r5 >> 2);
  const int g = (g6 << 2) | (g6 >> 4);
  const int b = (b5 << 3) | (b5 >> 2);
  return (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);  // Rec.601, /256
}

void pack_gray_landscape(const uint16_t* px, int w, int h, uint8_t* out) {
  const int row_bytes = w / 4;            // 200 for w=800
  const int total = (w * h) / 4;          // 96000 for 800x480
  std::memset(out, 0, (size_t)total);
  for (int y = 0; y < h; ++y) {
    const uint16_t* row = px + (size_t)y * w;
    uint8_t* orow = out + (size_t)y * row_bytes;
    for (int x = 0; x < w; ++x) {
      const uint8_t code = gray_code_from_lum(gray_lum565(row[x]));
      const int shift = 6 - ((x & 3) << 1);  // px0->6, px1->4, px2->2, px3->0
      orow[x >> 2] |= (uint8_t)(code << shift);
    }
  }
}

uint8_t gray_plane_byte(const uint8_t* g, int pixel8_base, int plane) {
  uint8_t o = 0;
  for (int k = 0; k < 8; ++k) {
    const uint8_t code = gray_code_at(g, pixel8_base + k);
    // plane 0 (RAM 0x24): set for black + light-gray; plane 1 (RAM 0x26): set
    // for black + dark-gray. Verbatim from EPD_3IN97_Display_4Gray.
    const uint8_t bit = plane == 0 ? (uint8_t)(code == 0 || code == 2)
                                   : (uint8_t)(code == 0 || code == 1);
    o = (uint8_t)((o << 1) | bit);
  }
  return o;
}

}  // namespace rcpaper
