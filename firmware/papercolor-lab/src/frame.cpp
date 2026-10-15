#include "frame.h"

namespace rclab {

void encode_native_row(const uint8_t* inks, size_t count, uint8_t* out) {
  for (size_t x = 0; x < count; x += 2) {
    out[x / 2] = uint8_t((inks[x] & 0x0F) << 4 | (inks[x + 1] & 0x0F));
  }
}

void encode_native_frame(const uint8_t* inks, uint8_t* out) {
  for (unsigned y = 0; y < kPanelHeight; ++y) {
    encode_native_row(inks + size_t(y) * kPanelWidth, kPanelWidth, out + size_t(y) * kNativeRowBytes);
  }
}

}  // namespace rclab
