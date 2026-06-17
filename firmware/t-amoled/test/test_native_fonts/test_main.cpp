#include <unity.h>

#include "render/fonts.h"

using namespace rcpaper;

void test_latin_page_text_never_uses_tiny_glcd_floor() {
  TEST_ASSERT_EQUAL_INT16(16, min_latin_font_height());
}

void test_sm_text_uses_tiny_label_height() {
  TEST_ASSERT_EQUAL_INT16(8, font_height(TextSize::Sm, Weight::Regular));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_latin_page_text_never_uses_tiny_glcd_floor);
  RUN_TEST(test_sm_text_uses_tiny_label_height);
  return UNITY_END();
}
