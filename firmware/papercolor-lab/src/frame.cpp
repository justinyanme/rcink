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

bool mono_light(uint8_t ink) { return ink == InkWhite || ink == InkYellow; }

void encode_mono(const uint8_t* inks, const uint8_t* old_plane, size_t count, uint8_t* out) {
  auto nibble = [&](size_t i) {
    const unsigned old = old_plane ? (old_plane[i / 8] >> (7 - i % 8)) & 1 : 0;
    return uint8_t(old << 1 | (mono_light(inks[i]) ? 1 : 0));
  };
  for (size_t i = 0; i < count; i += 2) out[i / 2] = uint8_t(nibble(i) << 4 | nibble(i + 1));
}

void encode_mono_2bpp(const uint8_t* inks, const uint8_t* old_plane, size_t count, uint8_t* out) {
  for (size_t i = 0; i < count; i += 4) {
    uint8_t byte = 0;
    for (size_t k = 0; k < 4; ++k) {
      const size_t p = i + k;
      const unsigned old = old_plane ? (old_plane[p / 8] >> (7 - p % 8)) & 1 : 0;
      byte = uint8_t(byte << 2 | old << 1 | (mono_light(inks[p]) ? 1 : 0));
    }
    out[i / 4] = byte;
  }
}

void mono_plane(const uint8_t* inks, size_t count, uint8_t* plane) {
  for (size_t i = 0; i < count; i += 8) {
    uint8_t bits = 0;
    for (size_t b = 0; b < 8; ++b) bits = uint8_t(bits << 1 | (mono_light(inks[i + b]) ? 1 : 0));
    plane[i / 8] = bits;
  }
}

void encode_mapped(const uint8_t* inks, const uint8_t map[8], size_t count, unsigned bits, uint8_t* out) {
  const size_t per_byte = 8 / bits;
  for (size_t i = 0; i < count; i += per_byte) {
    uint8_t byte = 0;
    for (size_t k = 0; k < per_byte; ++k) byte = uint8_t(byte << bits | map[inks[i + k] & 7]);
    out[i / per_byte] = byte;
  }
}

void encode_field(const uint8_t* inks, unsigned shift, unsigned width, size_t count, unsigned bits, uint8_t* out) {
  const size_t per_byte = 8 / bits;
  const unsigned mask = (1u << width) - 1;
  for (size_t i = 0; i < count; i += per_byte) {
    uint8_t byte = 0;
    for (size_t k = 0; k < per_byte; ++k) byte = uint8_t(byte << bits | ((inks[i + k] >> shift) & mask));
    out[i / per_byte] = byte;
  }
}

uint8_t staged_code(uint8_t ink, Palette palette) {
  // Computed with sf32rc ce0a01a encode() on uniform PALETTE fills:
  //   fast    black 03 white 15 yellow 3F red 2F blue 22 green 31
  //   quality black 03 white 0B yellow 3B red 27 blue 19 green 32
  const bool fast = palette == Palette::Fast;
  switch (ink) {
    case InkBlack: return 0x03;
    case InkWhite: return fast ? 0x15 : 0x0B;
    case InkYellow: return fast ? 0x3F : 0x3B;
    case InkRed: return fast ? 0x2F : 0x27;
    case InkBlue: return fast ? 0x22 : 0x19;
    case InkGreen: return fast ? 0x31 : 0x32;
  }
  return 0x03;  // unreachable for panel inks
}

uint8_t staged_selector(uint8_t code, bool high) { return high ? uint8_t(code >> 3) : uint8_t(code & 7); }

void encode_staged(const uint8_t* inks, size_t count, Palette palette, bool high, uint8_t* out) {
  for (size_t i = 0; i < count; i += 2) {
    out[i / 2] = uint8_t(staged_selector(staged_code(inks[i], palette), high) << 4 |
                         staged_selector(staged_code(inks[i + 1], palette), high));
  }
}

}  // namespace rclab
