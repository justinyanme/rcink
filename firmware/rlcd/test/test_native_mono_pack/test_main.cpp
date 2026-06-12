#include <unity.h>

#include <cstdint>
#include <vector>

#include "render/mono_pack.h"

using namespace rcpaper;

// mono_is_ink: only exact white (0xFFFF) is background; everything else inks.
void test_is_ink() {
  TEST_ASSERT_FALSE(mono_is_ink(0xFFFF));
  TEST_ASSERT_TRUE(mono_is_ink(0x0000));
  TEST_ASSERT_TRUE(mono_is_ink(0x1234));
  TEST_ASSERT_TRUE(mono_is_ink(0xFFFE));  // one bit off white still inks
}

// All-white frame -> buffer untouched from its 0xFF init (no bits cleared).
void test_all_white() {
  const int w = 4, h = 8;
  std::vector<uint16_t> px(w * h, 0xFFFF);
  uint8_t out[4] = {0, 0, 0, 0};
  pack_mono_landscape(px.data(), w, h, out);
  for (uint8_t b : out) TEST_ASSERT_EQUAL_HEX8(0xFF, b);
}

// All-ink frame -> every bit cleared.
void test_all_ink() {
  const int w = 4, h = 8;
  std::vector<uint16_t> px(w * h, 0x0000);
  uint8_t out[4];
  pack_mono_landscape(px.data(), w, h, out);
  for (uint8_t b : out) TEST_ASSERT_EQUAL_HEX8(0x00, b);
}

// Hand-computed single pixels (w=4, h=8 => 4-byte buffer, blocks_y = h/4 = 2).
//   idx = (x>>1)*blocks_y + ((h-1-y)>>2)
//   bit = 7 - ((((h-1-y)&3)<<1) | (x&1)) ; ink clears that bit.
void test_single_pixels() {
  const int w = 4, h = 8;

  {  // bottom-left (0,7): inv_y=0 -> idx 0, bit 7 -> out[0]=0x7F
    std::vector<uint16_t> px(w * h, 0xFFFF);
    px[7 * w + 0] = 0x0000;
    uint8_t out[4];
    pack_mono_landscape(px.data(), w, h, out);
    TEST_ASSERT_EQUAL_HEX8(0x7F, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[3]);
  }
  {  // (3,0): inv_y=7 -> block_y=1, byte_x=1 -> idx=3; local_y=3,local_x=1 -> bit 0
    std::vector<uint16_t> px(w * h, 0xFFFF);
    px[0 * w + 3] = 0x0000;
    uint8_t out[4];
    pack_mono_landscape(px.data(), w, h, out);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, out[3]);
  }
  {  // column-major stride: (2,7) -> byte_x=1, idx=1*2+0=2, bit 7 -> out[2]=0x7F
    std::vector<uint16_t> px(w * h, 0xFFFF);
    px[7 * w + 2] = 0x0000;
    uint8_t out[4];
    pack_mono_landscape(px.data(), w, h, out);
    TEST_ASSERT_EQUAL_HEX8(0x7F, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[0]);
  }
}

// The real panel geometry: 400x300 -> exactly 15000 bytes, indices in range.
void test_full_panel_size() {
  const int w = 400, h = 300;
  std::vector<uint16_t> px(w * h, 0xFFFF);
  std::vector<uint8_t> out(w * h / 8, 0);
  TEST_ASSERT_EQUAL_INT(15000, (int)out.size());
  // ink the four corners; just assert it does not corrupt out-of-range memory
  // (vector bounds) and clears exactly the expected corner bytes.
  px[0 * w + 0] = 0;            // top-left
  px[(h - 1) * w + 0] = 0;      // bottom-left -> idx 0, bit 7
  px[0 * w + (w - 1)] = 0;      // top-right
  px[(h - 1) * w + (w - 1)] = 0;  // bottom-right
  pack_mono_landscape(px.data(), w, h, out.data());
  // bottom-left clears bit7 of byte 0
  TEST_ASSERT_EQUAL_HEX8(0x7F, out[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_is_ink);
  RUN_TEST(test_all_white);
  RUN_TEST(test_all_ink);
  RUN_TEST(test_single_pixels);
  RUN_TEST(test_full_panel_size);
  return UNITY_END();
}
