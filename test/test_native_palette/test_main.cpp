#include <unity.h>
#include "render/palette.h"

using namespace rcpaper;

void test_known_tokens() {
  TEST_ASSERT_EQUAL(Color::Black,  color_from_token("black"));
  TEST_ASSERT_EQUAL(Color::White,  color_from_token("white"));
  TEST_ASSERT_EQUAL(Color::Red,    color_from_token("red"));
  TEST_ASSERT_EQUAL(Color::Yellow, color_from_token("yellow"));
  TEST_ASSERT_EQUAL(Color::Green,  color_from_token("green"));
  TEST_ASSERT_EQUAL(Color::Blue,   color_from_token("blue"));
}

void test_unknown_token_falls_back_to_black() {
  TEST_ASSERT_EQUAL(Color::Black, color_from_token("magenta"));
  TEST_ASSERT_EQUAL(Color::Black, color_from_token(nullptr));
  TEST_ASSERT_EQUAL(Color::Black, color_from_token(""));
}

void test_null_or_empty_explicit() {
  // also accept missing-color use case
  TEST_ASSERT_EQUAL(Color::Black, color_from_token("RED"));   // case-sensitive
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_known_tokens);
  RUN_TEST(test_unknown_token_falls_back_to_black);
  RUN_TEST(test_null_or_empty_explicit);
  return UNITY_END();
}
