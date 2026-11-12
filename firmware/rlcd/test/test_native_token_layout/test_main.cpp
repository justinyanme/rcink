#include <unity.h>

#include <algorithm>
#include <array>
#include <cstdint>

#include "render/token_layout.h"

using namespace rcpaper;

int vertical_gap(const TokenBox& upper, const TokenBox& lower) {
  return lower.top - upper.bottom;
}

bool overlaps(const TokenBox& a, const TokenBox& b) {
  return a.left < b.right && b.left < a.right &&
         a.top < b.bottom && b.top < a.bottom;
}

void test_worst_case_token_rows_have_real_glyph_and_element_gaps() {
  // FreeSans advances used by the device, measured from its GFX glyph tables:
  // Gemini Bold9=61; widest 99.9M/9.99M Bold24=131; the capped worst today
  // line Regular9=278 plus four for the drawn dot; ODO+999B Bold9=91.
  const TokenTextWidths worst = {61, 131, 282, 91};
  for (size_t i = 0; i < kTokenProviderCount; i++) {
    const TokenRowGeometry row = token_row_geometry(i, worst);
    TEST_ASSERT_GREATER_OR_EQUAL(2, vertical_gap(row.provider, row.total));
    TEST_ASSERT_GREATER_OR_EQUAL(4, vertical_gap(row.seven_days, row.chart));
    TEST_ASSERT_GREATER_OR_EQUAL(4, vertical_gap(row.chart, row.today));
    TEST_ASSERT_GREATER_OR_EQUAL(4, vertical_gap(row.odometer, row.today));

    const std::array<TokenBox, 6> boxes = {
        row.provider, row.seven_days, row.total, row.chart, row.today, row.odometer};
    for (size_t a = 0; a < boxes.size(); a++) {
      TEST_ASSERT_GREATER_OR_EQUAL(0, boxes[a].left);
      TEST_ASSERT_LESS_OR_EQUAL(400, boxes[a].right);
      TEST_ASSERT_GREATER_OR_EQUAL(0, boxes[a].top);
      TEST_ASSERT_LESS_OR_EQUAL(300, boxes[a].bottom);
      for (size_t b = a + 1; b < boxes.size(); b++) {
        TEST_ASSERT_FALSE(overlaps(boxes[a], boxes[b]));
      }
    }
    const int divider_y = 26 + (int)i * 82 + 81;
    TEST_ASSERT_GREATER_OR_EQUAL(4, divider_y - row.today.bottom);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_worst_case_token_rows_have_real_glyph_and_element_gaps);
  return UNITY_END();
}
