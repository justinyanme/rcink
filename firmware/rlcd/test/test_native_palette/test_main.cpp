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

void test_semantic_tokens() {
  Color c = Color::Black;
  TEST_ASSERT_TRUE(color_from_token("accent", &c));
  TEST_ASSERT_EQUAL(Color::Red, c);
  TEST_ASSERT_TRUE(color_from_token("warning", &c));
  TEST_ASSERT_EQUAL(Color::Yellow, c);
  TEST_ASSERT_TRUE(color_from_token("success", &c));
  TEST_ASSERT_EQUAL(Color::Green, c);
  TEST_ASSERT_TRUE(color_from_token("info", &c));
  TEST_ASSERT_EQUAL(Color::Blue, c);
  TEST_ASSERT_TRUE(color_from_token("muted", &c));
  TEST_ASSERT_EQUAL(Color::Black, c);
}

void test_unknown_token_is_invalid() {
  Color c = Color::White;
  TEST_ASSERT_FALSE(color_from_token("magenta", &c));
  TEST_ASSERT_EQUAL(Color::White, c);
  TEST_ASSERT_FALSE(color_from_token(nullptr, &c));
  TEST_ASSERT_FALSE(color_from_token("", &c));
  TEST_ASSERT_FALSE(color_from_token("RED", &c));
}

void test_color_tokens_and_rgb() {
  TEST_ASSERT_EQUAL_STRING("black", color_to_token(Color::Black));
  TEST_ASSERT_EQUAL_STRING("white", color_to_token(Color::White));
  TEST_ASSERT_EQUAL_STRING("red", color_to_token(Color::Red));
  TEST_ASSERT_EQUAL_STRING("yellow", color_to_token(Color::Yellow));
  TEST_ASSERT_EQUAL_STRING("green", color_to_token(Color::Green));
  TEST_ASSERT_EQUAL_STRING("blue", color_to_token(Color::Blue));

  TEST_ASSERT_EQUAL_HEX32(kRgbBlack, color_to_rgb(Color::Black));
  TEST_ASSERT_EQUAL_HEX32(kRgbWhite, color_to_rgb(Color::White));
  TEST_ASSERT_EQUAL_HEX32(kRgbRed, color_to_rgb(Color::Red));
  TEST_ASSERT_EQUAL_HEX32(kRgbYellow, color_to_rgb(Color::Yellow));
  TEST_ASSERT_EQUAL_HEX32(kRgbGreen, color_to_rgb(Color::Green));
  TEST_ASSERT_EQUAL_HEX32(kRgbBlue, color_to_rgb(Color::Blue));
}

void test_rgb565_conversion() {
  TEST_ASSERT_EQUAL_HEX16(0x0000, color_to_rgb565(Color::Black));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, color_to_rgb565(Color::White));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_known_tokens);
  RUN_TEST(test_semantic_tokens);
  RUN_TEST(test_unknown_token_is_invalid);
  RUN_TEST(test_color_tokens_and_rgb);
  RUN_TEST(test_rgb565_conversion);
  return UNITY_END();
}
