#include "render/mono_pack.h"

#include <cstring>

namespace rcpaper {

void pack_mono_landscape(const uint16_t* px, int w, int h, uint8_t* out) {
  const int bytes = (w * h) / 8;
  std::memset(out, 0xFF, (size_t)bytes);
  const int blocks_y = h / 4;  // 75 for h=300
  for (int y = 0; y < h; ++y) {
    const int inv_y = h - 1 - y;
    const int block_y = inv_y >> 2;
    const int local_y = inv_y & 3;
    const uint16_t* row = px + (size_t)y * w;
    for (int x = 0; x < w; ++x) {
      if (!mono_is_ink(row[x])) continue;
      const int idx = (x >> 1) * blocks_y + block_y;
      const int bit = 7 - (((local_y << 1) | (x & 1)));
      out[idx] &= (uint8_t)~(1 << bit);
    }
  }
}

}  // namespace rcpaper
