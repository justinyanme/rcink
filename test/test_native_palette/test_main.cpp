#include <unity.h>
#include "render/palette.h"

using namespace rcpaper;

void test_known_tokens() {
  Color c = Color::White;
  TEST_ASSERT_TRUE(color_from_token("black", &c));
  TEST_ASSERT_EQUAL(Color::Black, c);
  TEST_ASSERT_TRUE(color_from_token("white", &c));
  TEST_ASSERT_EQUAL(Color::White, c);
  TEST_ASSERT_TRUE(color_from_token("red", &c));
  TEST_ASSERT_EQUAL(Color::Red, c);
  TEST_ASSERT_TRUE(color_from_token("yellow", &c));
  TEST_ASSERT_EQUAL(Color::Yellow, c);
  TEST_ASSERT_TRUE(color_from_token("green", &c));
  TEST_ASSERT_EQUAL(Color::Green, c);
  TEST_ASSERT_TRUE(color_from_token("blue", &c));
  TEST_ASSERT_EQUAL(Color::Blue, c);
}

void test_unknown_token_is_invalid() {
  Color c = Color::White;
  TEST_ASSERT_FALSE(color_from_token("magenta", &c));
  TEST_ASSERT_EQUAL(Color::White, c);
  TEST_ASSERT_FALSE(color_from_token(nullptr, &c));
  TEST_ASSERT_FALSE(color_from_token("", &c));
  TEST_ASSERT_FALSE(color_from_token("RED", &c));
}

void test_color_tokens_and_rgb_are_e6_solid_colors() {
  TEST_ASSERT_EQUAL_STRING("black", color_to_token(Color::Black));
  TEST_ASSERT_EQUAL_STRING("white", color_to_token(Color::White));
  TEST_ASSERT_EQUAL_STRING("red", color_to_token(Color::Red));
  TEST_ASSERT_EQUAL_STRING("yellow", color_to_token(Color::Yellow));
  TEST_ASSERT_EQUAL_STRING("green", color_to_token(Color::Green));
  TEST_ASSERT_EQUAL_STRING("blue", color_to_token(Color::Blue));

  TEST_ASSERT_EQUAL_HEX32(0x000000, kE6Black);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, kE6White);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000, kE6Red);
  TEST_ASSERT_EQUAL_HEX32(0xFFFF00, kE6Yellow);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, kE6Green);
  TEST_ASSERT_EQUAL_HEX32(0x0000FF, kE6Blue);
}

void test_quantize_rgb_to_e6_nearest_solid_color() {
  TEST_ASSERT_EQUAL_HEX32(kE6Red, quantize_rgb_to_e6(220, 20, 20));
  TEST_ASSERT_EQUAL_HEX32(kE6Yellow, quantize_rgb_to_e6(230, 220, 20));
  TEST_ASSERT_EQUAL_HEX32(kE6Green, quantize_rgb_to_e6(20, 220, 20));
  TEST_ASSERT_EQUAL_HEX32(kE6Blue, quantize_rgb_to_e6(20, 20, 220));
  TEST_ASSERT_EQUAL_HEX32(kE6White, quantize_rgb_to_e6(230, 230, 230));
  TEST_ASSERT_EQUAL_HEX32(kE6Black, quantize_rgb_to_e6(20, 20, 20));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_known_tokens);
  RUN_TEST(test_unknown_token_is_invalid);
  RUN_TEST(test_color_tokens_and_rgb_are_e6_solid_colors);
  RUN_TEST(test_quantize_rgb_to_e6_nearest_solid_color);
  return UNITY_END();
}
