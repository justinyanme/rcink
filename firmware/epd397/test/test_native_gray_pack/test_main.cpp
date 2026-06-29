#include <unity.h>

#include <cstdint>
#include <vector>

#include "render/gray_pack.h"

using namespace rcpaper;

// Luminance -> 4-level code: pure white = 3, pure black = 0, monotonic between.
void test_lum_and_code() {
  TEST_ASSERT_EQUAL_UINT8(255, gray_lum565(0xFFFF));  // white
  TEST_ASSERT_EQUAL_UINT8(0, gray_lum565(0x0000));    // black
  TEST_ASSERT_EQUAL_UINT8(3, gray_code_from_lum(255));
  TEST_ASSERT_EQUAL_UINT8(0, gray_code_from_lum(0));
  TEST_ASSERT_EQUAL_UINT8(1, gray_code_from_lum(85));
  TEST_ASSERT_EQUAL_UINT8(2, gray_code_from_lum(170));
  TEST_ASSERT_EQUAL_UINT8(3, gray_code_from_lum(255));
}

// All-white frame -> every code 3 -> every byte 0xFF (0b11_11_11_11).
void test_all_white() {
  const int w = 8, h = 2;  // 4 bytes
  std::vector<uint16_t> px(w * h, 0xFFFF);
  uint8_t out[4] = {0, 0, 0, 0};
  pack_gray_landscape(px.data(), w, h, out);
  for (uint8_t b : out) TEST_ASSERT_EQUAL_HEX8(0xFF, b);
}

// All-black frame -> every code 0 -> every byte 0x00.
void test_all_black() {
  const int w = 8, h = 2;
  std::vector<uint16_t> px(w * h, 0x0000);
  uint8_t out[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  pack_gray_landscape(px.data(), w, h, out);
  for (uint8_t b : out) TEST_ASSERT_EQUAL_HEX8(0x00, b);
}

// MSB-first within a byte + row-major stride (w=8 -> 2 bytes/row).
void test_pixel_positions() {
  const int w = 8, h = 2;
  {  // white at (0,0): bits[7:6] of out[0] = 0b11 -> 0xC0
    std::vector<uint16_t> px(w * h, 0x0000);
    px[0] = 0xFFFF;
    uint8_t out[4];
    pack_gray_landscape(px.data(), w, h, out);
    TEST_ASSERT_EQUAL_HEX8(0xC0, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[1]);
  }
  {  // white at (5,0): idx=1, shift=4 -> 0x30
    std::vector<uint16_t> px(w * h, 0x0000);
    px[5] = 0xFFFF;
    uint8_t out[4];
    pack_gray_landscape(px.data(), w, h, out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x30, out[1]);
  }
  {  // white at (0,1) -> second row, out[2]
    std::vector<uint16_t> px(w * h, 0x0000);
    px[w + 0] = 0xFFFF;
    uint8_t out[4];
    pack_gray_landscape(px.data(), w, h, out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xC0, out[2]);
  }
}

void test_code_extraction() {
  // byte 0x1B encodes codes [0,1,2,3] MSB-first.
  uint8_t g[2] = {0x1B, 0x1B};
  TEST_ASSERT_EQUAL_UINT8(0, gray_code_at(g, 0));
  TEST_ASSERT_EQUAL_UINT8(1, gray_code_at(g, 1));
  TEST_ASSERT_EQUAL_UINT8(2, gray_code_at(g, 2));
  TEST_ASSERT_EQUAL_UINT8(3, gray_code_at(g, 3));
}

// Dual-plane split matches the vendor EPD_3IN97_Display_4Gray mapping:
//   plane 0 (RAM 0x24) bit set for codes {0 (black), 2 (light gray)}
//   plane 1 (RAM 0x26) bit set for codes {0 (black), 1 (dark gray)}
void test_plane_split() {
  // 8 pixels, codes [0,1,2,3,0,1,2,3].
  uint8_t g[2] = {0x1B, 0x1B};
  // plane0 bits: 1,0,1,0,1,0,1,0 -> 0xAA
  TEST_ASSERT_EQUAL_HEX8(0xAA, gray_plane_byte(g, 0, 0));
  // plane1 bits: 1,1,0,0,1,1,0,0 -> 0xCC
  TEST_ASSERT_EQUAL_HEX8(0xCC, gray_plane_byte(g, 0, 1));
}

// Fast-refresh mono threshold: white(1) for codes {2,3}, black(0) for {0,1}.
void test_mono_threshold() {
  uint8_t g[2] = {0x1B, 0x1B};  // codes [0,1,2,3,0,1,2,3]
  // white bits: 0,0,1,1,0,0,1,1 -> 0x33
  TEST_ASSERT_EQUAL_HEX8(0x33, gray_to_mono_byte(g, 0));
}

// Real panel geometry: 800x480 -> exactly 96000 bytes, no out-of-range writes.
void test_full_panel_size() {
  const int w = 800, h = 480;
  std::vector<uint16_t> px(w * h, 0xFFFF);
  std::vector<uint8_t> out(w * h / 4, 0);
  TEST_ASSERT_EQUAL_INT(96000, (int)out.size());
  px[0] = 0x0000;  // top-left black -> code 0 in bits[7:6] of byte 0
  pack_gray_landscape(px.data(), w, h, out.data());
  TEST_ASSERT_EQUAL_HEX8(0x3F, out[0]);  // 0b00_11_11_11 (px0 black, px1..3 white)
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_lum_and_code);
  RUN_TEST(test_all_white);
  RUN_TEST(test_all_black);
  RUN_TEST(test_pixel_positions);
  RUN_TEST(test_code_extraction);
  RUN_TEST(test_plane_split);
  RUN_TEST(test_mono_threshold);
  RUN_TEST(test_full_panel_size);
  return UNITY_END();
}
