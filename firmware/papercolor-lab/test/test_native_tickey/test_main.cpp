// TICKEY encoders against sf32rc ce0a01a's documented bytes:
//   mono: tests/v2_tickey_mono_test.c:25-29,96-100 (fixture inks 1,0,2,5,3,6),
//         docs/dayotag-v2-tickey-research.md:85-92 (nibble = old<<1 | new)
//   colour: docs/dayotag-v2-tickey-color.md:66-71,84-93 (selector split, packing)
#include <unity.h>

#include "frame.h"
#include "waveforms.h"

using namespace rclab;

// sf32rc fixture row: white, black, yellow, blue, red, green.
static const uint8_t kRow[] = {InkWhite, InkBlack, InkYellow, InkBlue, InkRed, InkGreen};

void test_mono_cold_row() {
  uint8_t out[3];
  encode_mono(kRow, nullptr, 6, out);
  const uint8_t expected[] = {0x10, 0x10, 0x00};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 3);
}

void test_mono_warm_unchanged_row() {
  const uint8_t old_plane[] = {0xA0};  // light bits 1,0,1,0,0,0 (+2 pad)
  uint8_t out[3];
  encode_mono(kRow, old_plane, 6, out);
  const uint8_t expected[] = {0x30, 0x30, 0x00};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 3);
}

void test_mono_inversion_row() {
  // New light bits 0,1,0,1,1,1 over old 1,0,1,0,0,0.
  const uint8_t inverted[] = {InkBlack, InkWhite, InkRed, InkYellow, InkWhite, InkYellow};
  const uint8_t old_plane[] = {0xA0};
  uint8_t out[3];
  encode_mono(inverted, old_plane, 6, out);
  const uint8_t expected[] = {0x21, 0x21, 0x11};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 3);
}

void test_mono_2bpp_packing() {
  // Lab variable: same transition codes, 4 per byte, first pixel in bits 7:6.
  // Inversion fixture codes 2,1,2,1,1,1 (+ cold 0,0) -> 10 01 10 01 | 01 01 00 00.
  const uint8_t inks[] = {InkBlack, InkWhite, InkRed, InkYellow, InkWhite, InkYellow, InkBlack, InkBlack};
  const uint8_t old_plane[] = {0xA0};
  uint8_t out[2];
  encode_mono_2bpp(inks, old_plane, 8, out);
  const uint8_t expected[] = {0x99, 0x50};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 2);
}

void test_mono_light_rule() {
  // tickey_mono.c:21-25: only white and yellow are light.
  TEST_ASSERT_TRUE(mono_light(InkWhite));
  TEST_ASSERT_TRUE(mono_light(InkYellow));
  TEST_ASSERT_FALSE(mono_light(InkBlack));
  TEST_ASSERT_FALSE(mono_light(InkRed));
  TEST_ASSERT_FALSE(mono_light(InkBlue));
  TEST_ASSERT_FALSE(mono_light(InkGreen));
  const uint8_t eight[] = {InkWhite, InkBlack, InkYellow, InkBlue, InkRed, InkGreen, InkWhite, InkYellow};
  uint8_t plane = 0;
  mono_plane(eight, 8, &plane);
  TEST_ASSERT_EQUAL_HEX8(0xA3, plane);  // 1010 0011, MSB = first pixel
}

void test_staged_selector_split() {
  // tickey-color.md:66-71: fast 15=(2,5), 3F=(7,7); quality 0B=(1,3), 3C=(7,4).
  const uint8_t codes[][3] = {{0x15, 2, 5}, {0x3F, 7, 7}, {0x0B, 1, 3}, {0x3C, 7, 4}};
  for (const auto& c : codes) {
    TEST_ASSERT_EQUAL(c[1], staged_selector(c[0], true));
    TEST_ASSERT_EQUAL(c[2], staged_selector(c[0], false));
  }
}

void test_staged_black_pins_code3() {
  // v2_tickey_color.py:90-96: exact black emits code 3 in both palettes.
  TEST_ASSERT_EQUAL_HEX8(0x03, staged_code(InkBlack, Palette::Fast));
  TEST_ASSERT_EQUAL_HEX8(0x03, staged_code(InkBlack, Palette::Quality));
}

void test_staged_plane_packing() {
  // black (03), white: fast 15 / quality 0B (sf32rc encode() of PALETTE[1],
  // equal to the documented declared-light codes 21 / 11).
  const uint8_t inks[] = {InkBlack, InkWhite};
  uint8_t out = 0;
  encode_staged(inks, 2, Palette::Fast, true, &out);
  TEST_ASSERT_EQUAL_HEX8(0x02, out);  // (03>>3, 15>>3)
  encode_staged(inks, 2, Palette::Fast, false, &out);
  TEST_ASSERT_EQUAL_HEX8(0x35, out);  // (03&7, 15&7)
  encode_staged(inks, 2, Palette::Quality, true, &out);
  TEST_ASSERT_EQUAL_HEX8(0x01, out);
  encode_staged(inks, 2, Palette::Quality, false, &out);
  TEST_ASSERT_EQUAL_HEX8(0x33, out);
}

void test_lut_lengths() {
  // TICKEY uploader SPI transaction 0x18F8 bits (research.md:45) = 799 bytes.
  constexpr size_t kSourceBytes = 0x18F8 / 8;
  TEST_ASSERT_EQUAL(kSourceBytes, sizeof(tickey_waveform_clear_initial));
  TEST_ASSERT_EQUAL(kSourceBytes, sizeof(tickey_waveform_mono));
  TEST_ASSERT_EQUAL(kSourceBytes, sizeof(tickey_waveform_mono_clear));
  TEST_ASSERT_EQUAL(kSourceBytes, sizeof(tickey_waveform_color_fast_phase1));
  TEST_ASSERT_EQUAL(kSourceBytes, sizeof(tickey_waveform_color_fast_phase2));
  TEST_ASSERT_EQUAL(kSourceBytes, sizeof(tickey_waveform_color_quality_phase1));
  TEST_ASSERT_EQUAL(kSourceBytes, sizeof(tickey_waveform_color_quality_phase2));
  TEST_ASSERT_EQUAL(kSourceBytes, sizeof(tickey_waveform_color_quality_phase3));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mono_cold_row);
  RUN_TEST(test_mono_warm_unchanged_row);
  RUN_TEST(test_mono_inversion_row);
  RUN_TEST(test_mono_2bpp_packing);
  RUN_TEST(test_mono_light_rule);
  RUN_TEST(test_staged_selector_split);
  RUN_TEST(test_staged_black_pins_code3);
  RUN_TEST(test_staged_plane_packing);
  RUN_TEST(test_lut_lengths);
  return UNITY_END();
}
