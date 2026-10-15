// Built-in images against hand-counted coordinates and the VT323 font bitmap.
#include <unity.h>

#include <cstring>
#include <vector>

#include "image.h"

using namespace rclab;

static std::vector<uint8_t> plane(kPixels);

static uint8_t at(unsigned x, unsigned y) { return plane[size_t(y) * kPanelWidth + x]; }

void test_chart_layout() {
  render_chart(plane.data());
  // Border: 4 px black on every edge.
  TEST_ASSERT_EQUAL(InkBlack, at(0, 0));
  TEST_ASSERT_EQUAL(InkBlack, at(3, 150));
  TEST_ASSERT_EQUAL(InkBlack, at(396, 50));
  TEST_ASSERT_EQUAL(InkBlack, at(200, 596));
  // Patches: 200x100, rows black|white, yellow|red, blue|green.
  TEST_ASSERT_EQUAL(InkBlack, at(199, 50));
  TEST_ASSERT_EQUAL(InkWhite, at(200, 50));
  TEST_ASSERT_EQUAL(InkWhite, at(395, 99));
  TEST_ASSERT_EQUAL(InkYellow, at(100, 100));
  TEST_ASSERT_EQUAL(InkRed, at(300, 199));
  TEST_ASSERT_EQUAL(InkBlue, at(4, 200));
  TEST_ASSERT_EQUAL(InkGreen, at(300, 299));
  // Ramps from x=4 (value 2: black) to x=300 (value 191: yellow, by hand:
  // (191,181,41) is 8165 from yellow, 17394 from green, 34442 from red).
  TEST_ASSERT_EQUAL(InkBlack, at(4, 300));
  TEST_ASSERT_EQUAL(InkYellow, at(300, 300));
  TEST_ASSERT_EQUAL(InkRed, at(395, 350));
  TEST_ASSERT_EQUAL(InkBlue, at(395, 400));
  TEST_ASSERT_EQUAL(InkGreen, at(395, 499));
  TEST_ASSERT_EQUAL(InkBlack, at(4, 499));
  // Footer checker, 20 px: (x/20 + y/20) odd is white.
  TEST_ASSERT_EQUAL(InkWhite, at(4, 500));
  TEST_ASSERT_EQUAL(InkBlack, at(20, 500));
  TEST_ASSERT_EQUAL(InkBlack, at(4, 520));
  TEST_ASSERT_EQUAL(InkWhite, at(4, 595));
}

void test_checker_parity() {
  render_checker(plane.data(), 40);
  TEST_ASSERT_EQUAL(InkBlack, at(0, 0));
  TEST_ASSERT_EQUAL(InkBlack, at(39, 39));
  TEST_ASSERT_EQUAL(InkWhite, at(40, 0));
  TEST_ASSERT_EQUAL(InkWhite, at(0, 40));
  TEST_ASSERT_EQUAL(InkBlack, at(40, 40));
  TEST_ASSERT_EQUAL(InkWhite, at(399, 599));  // 9 + 14 = 23
  render_checker(plane.data(), 1);
  TEST_ASSERT_EQUAL(InkWhite, at(1, 0));
  TEST_ASSERT_EQUAL(InkBlack, at(1, 1));
}

void test_invert_row() {
  uint8_t row[] = {InkBlack, InkWhite, InkYellow, InkRed, InkBlue, InkGreen};
  const uint8_t expected[] = {InkWhite, InkBlack, InkYellow, InkRed, InkBlue, InkGreen};
  invert(row, sizeof(row));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, row, sizeof(row));
}

// VT323 16 pt 'A' (0x41): glyph {518, 10, 18, 13, 1, -17} in
// ../papercolor/src/render/fonts/vt323_16pt7b.h, bytes 1e 07 81 f0 7c 1f 0c e3
// 38 ce 71 9c 67 f9 fe 7f b8 3e 0f 83 e0 f8 30 decoded MSB first by hand.
static const char* const kGlyphA[] = {
    "...####...", "...####...", "...#####..", "...#####..", "...#####..", "..##..###.",
    "..##..###.", "..##..###.", ".###...##.", ".###...##.", ".########.", ".########.",
    ".########.", "###.....##", "###.....##", "###.....##", "###.....##", "###.....##",
};

void test_vt323_glyph_bits() {
  render_fill(plane.data(), InkWhite);
  const unsigned end = draw_text(plane.data(), FontSize::Pt16, 20, 40, "A", InkBlack);
  TEST_ASSERT_EQUAL(33, end);  // xAdvance 13
  size_t expected_black = 0;
  for (unsigned row = 0; row < 18; ++row) {
    for (unsigned col = 0; col < 10; ++col) {
      const bool on = kGlyphA[row][col] == '#';
      expected_black += on;
      // Upper-left = cursor + (xOffset 1, yOffset -17) = (21, 23).
      TEST_ASSERT_EQUAL_MESSAGE(on ? InkBlack : InkWhite, at(21 + col, 23 + row), kGlyphA[row]);
    }
  }
  size_t black = 0;
  for (uint8_t ink : plane) black += ink == InkBlack;
  TEST_ASSERT_EQUAL(expected_black, black);  // nothing drawn outside the glyph box
}

void test_text_inverse_band() {
  render_text(plane.data());
  // Some row near the bottom is a full black band edge to edge (white-on-black line).
  bool band = false;
  for (unsigned y = 300; y < kPanelHeight && !band; ++y) {
    band = at(0, y) == InkBlack && at(399, y) == InkBlack && at(200, y) == InkBlack;
  }
  TEST_ASSERT_TRUE(band);
  TEST_ASSERT_EQUAL(InkWhite, at(0, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_chart_layout);
  RUN_TEST(test_checker_parity);
  RUN_TEST(test_invert_row);
  RUN_TEST(test_vt323_glyph_bits);
  RUN_TEST(test_text_inverse_band);
  return UNITY_END();
}
