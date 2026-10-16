// Native packing against pinned Panel_ED2208.cpp _dither_row_none:
//   dst[x >> 1] = (c0 << 4) | c1   with c0 the left pixel.
#include <unity.h>

#include "frame.h"

using namespace rclab;

void test_native_row_four_pixels() {
  // Hand-built row: black, white, red, green → 0x01, 0x36.
  const uint8_t inks[] = {InkBlack, InkWhite, InkRed, InkGreen};
  uint8_t out[2] = {0xEE, 0xEE};
  encode_native_row(inks, 4, out);
  const uint8_t expected[] = {0x01, 0x36};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 2);
}

void test_native_row_yellow_blue_order() {
  // yellow, blue → 0x25; blue, yellow → 0x52 (left pixel high nibble).
  const uint8_t a[] = {InkYellow, InkBlue}, b[] = {InkBlue, InkYellow};
  uint8_t out = 0;
  encode_native_row(a, 2, &out);
  TEST_ASSERT_EQUAL_HEX8(0x25, out);
  encode_native_row(b, 2, &out);
  TEST_ASSERT_EQUAL_HEX8(0x52, out);
}

void test_native_frame_size_and_full_fields() {
  // 400x600 at two pixels per byte is 120000 bytes (pinned row_bytes = (w+1)>>1).
  TEST_ASSERT_EQUAL(120000, kNativeFrameBytes);
  static uint8_t inks[kPixels];
  static uint8_t out[kNativeFrameBytes];
  for (auto& p : inks) p = InkWhite;
  encode_native_frame(inks, out);
  TEST_ASSERT_EQUAL_HEX8(0x11, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x11, out[kNativeFrameBytes - 1]);
  for (auto& p : inks) p = InkBlack;
  encode_native_frame(inks, out);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[kNativeFrameBytes / 2]);
}

void test_mapped_two_and_four_bits() {
  // map: black->1, white->0, yellow->3, red->2; hand-packed bytes.
  const uint8_t map[8] = {1, 0, 3, 2, 0, 0, 0, 0};
  const uint8_t inks[] = {InkBlack, InkWhite, InkYellow, InkRed};
  uint8_t out[2] = {0xEE, 0xEE};
  encode_mapped(inks, map, 4, 2, out);  // 01 00 11 10
  TEST_ASSERT_EQUAL_HEX8(0x4E, out[0]);
  encode_mapped(inks, map, 4, 4, out);
  const uint8_t expected[] = {0x10, 0x32};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 2);
}

void test_field_selects_bits() {
  // values 0b011011, 0b111111, 0, 0b100100; hand-packed.
  const uint8_t v[] = {0x1B, 0x3F, 0x00, 0x24};
  uint8_t out[2] = {};
  encode_field(v, 0, 2, 4, 2, out);  // bits 1:0 -> 3 3 0 0
  TEST_ASSERT_EQUAL_HEX8(0xF0, out[0]);
  encode_field(v, 2, 2, 4, 2, out);  // bits 3:2 -> 2 3 0 1
  TEST_ASSERT_EQUAL_HEX8(0xB1, out[0]);
  encode_field(v, 0, 3, 4, 4, out);  // bits 2:0 at 4 bpp -> 3 7 | 0 4
  TEST_ASSERT_EQUAL_HEX8(0x37, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x04, out[1]);
  encode_field(v, 5, 1, 4, 2, out);  // bit 5 -> 0 1 0 1
  TEST_ASSERT_EQUAL_HEX8(0x11, out[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_native_row_four_pixels);
  RUN_TEST(test_native_row_yellow_blue_order);
  RUN_TEST(test_native_frame_size_and_full_fields);
  RUN_TEST(test_mapped_two_and_four_bits);
  RUN_TEST(test_field_selects_bits);
  return UNITY_END();
}
