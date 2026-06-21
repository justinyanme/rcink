#include <unity.h>

#include "render/fonts.h"

using namespace rcpaper;

void test_latin_page_text_never_uses_tiny_glcd_floor() {
  TEST_ASSERT_EQUAL_INT16(16, min_latin_font_height());
}

void test_sm_labels_are_readable_not_tiny() {
  // Small labels / fine print sit at the 12pt rung, not the old tiny 9pt.
  TEST_ASSERT_EQUAL_INT16(24, font_height(TextSize::Sm, Weight::Regular));
}

void test_title_and_body_form_a_gentle_hierarchy() {
  // Titles (H1) are 24pt and body 18pt — prominent but not the old huge 2x.
  TEST_ASSERT_EQUAL_INT16(52, font_height(TextSize::H1, Weight::Regular));
  TEST_ASSERT_EQUAL_INT16(34, font_height(TextSize::Body, Weight::Regular));
  TEST_ASSERT_TRUE(font_height(TextSize::H1, Weight::Regular) > font_height(TextSize::Body, Weight::Regular));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_latin_page_text_never_uses_tiny_glcd_floor);
  RUN_TEST(test_sm_labels_are_readable_not_tiny);
  RUN_TEST(test_title_and_body_form_a_gentle_hierarchy);
  return UNITY_END();
}
