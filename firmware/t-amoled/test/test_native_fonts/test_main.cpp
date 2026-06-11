#include <unity.h>

#include "render/fonts.h"

using namespace rcpaper;

void test_latin_page_text_never_uses_tiny_glcd_floor() {
  TEST_ASSERT_EQUAL_INT16(16, min_latin_font_height());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_latin_page_text_never_uses_tiny_glcd_floor);
  return UNITY_END();
}
