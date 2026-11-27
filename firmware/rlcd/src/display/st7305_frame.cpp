#include "display/st7305_frame.h"

#include <cstring>

namespace rcpaper {
namespace {

constexpr uint8_t first_col = 0x12, last_col = 0x2a;
constexpr int rows = St7305Frame::width / 2;
constexpr int stride = St7305Frame::height / 4;  // bytes per controller row
constexpr int bitmap_stride = St7305Frame::width / 8;
static_assert(stride == (last_col - first_col + 1) * 3);

// Four consecutive pairs of ink bits become one panel byte, bottom pair first.
uint8_t pack_pairs(const uint8_t* bottom, int shift) {
  unsigned ink = 0;
  for (int y = 0; y < 4; ++y)
    ink = (ink << 2) | ((bottom[-y * bitmap_stride] >> shift) & 3);
  return static_cast<uint8_t>(~ink);
}

}  // namespace

void St7305Frame::push(const uint16_t* pixels, MonoPolarity polarity) {
  pack_mono_landscape(pixels, width, height, bytes_.data(), polarity);
  write_(first_col, last_col, 0, rows - 1, bytes_.data(), bytes_.size());
}

void St7305Frame::push_changes(const uint8_t* bitmap) {
  // One CASET address spans 12 pixels vertically and takes three bytes per
  // RASET row. With MADCTL MX, the bottom band has the highest CASET address.
  uint8_t band[rows * 3];
  for (int col = first_col; col <= last_col; ++col) {
    const int block = (last_col - col) * 3;
    const int bottom = height - 1 - 4 * block;
    int first = -1, last = -1;
    for (int row = 0; row <= rows; ++row) {
      if (row < rows) {
        const uint8_t* src = bitmap + bottom * bitmap_stride + row / 4;
        const int shift = 6 - 2 * (row % 4);
        uint8_t* unit = band + 3 * row;
        for (int b = 0; b < 3; ++b)
          unit[b] = pack_pairs(src - b * 4 * bitmap_stride, shift);
        uint8_t* shown = bytes_.data() + row * stride + block;
        if (std::memcmp(unit, shown, 3) != 0) {
          std::memcpy(shown, unit, 3);
          if (first < 0) first = row;
          last = row;
        }
      }
      // Bridging two clean units costs six bytes, less than another pair of
      // address commands and RAMWR. A clean band never touches the SPI bus.
      if (first >= 0 && (row == rows || row - last > 2)) {
        write_(col, col, first, last, band + first * 3, (last - first + 1) * 3);
        first = -1;
      }
    }
  }
}

}  // namespace rcpaper
