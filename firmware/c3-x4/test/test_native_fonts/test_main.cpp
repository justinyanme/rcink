#include <unity.h>

#include "render/fonts.h"

using namespace rcpaper;

void test_latin_page_text_never_uses_tiny_glcd_floor() {
  TEST_ASSERT_EQUAL_INT16(16, min_latin_font_height());
}

void test_sm_text_uses_crisp_label_height() {
  // The 800x480 X4 is high-res, so even small labels use the crisp 9pt rung
  // (16px) rather than the tiny GLCD floor the smaller panels fall back to.
  TEST_ASSERT_EQUAL_INT16(16, font_height(TextSize::Sm, Weight::Regular));
}

void test_detail_masthead_uses_big_2x_rung() {
  // H1 (detail-page mastheads + big stat numbers) gets the large 2x rung.
  TEST_ASSERT_EQUAL_INT16(80, font_height(TextSize::H1, Weight::Regular));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_latin_page_text_never_uses_tiny_glcd_floor);
  RUN_TEST(test_sm_text_uses_crisp_label_height);
  RUN_TEST(test_detail_masthead_uses_big_2x_rung);
  return UNITY_END();
}
