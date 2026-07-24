#include <unity.h>

#include "input/navigation.h"

using namespace rcpaper;

void test_key_enters_deck_from_dashboard() {
  const NavigationState next =
      navigation_after_key_press(Mode::Dashboard, 0, 3);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Mode::Deck),
                          static_cast<uint8_t>(next.mode));
  TEST_ASSERT_EQUAL_UINT(0, next.page_index);
}

void test_key_advances_to_next_deck_page() {
  const NavigationState next =
      navigation_after_key_press(Mode::Deck, 0, 3);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Mode::Deck),
                          static_cast<uint8_t>(next.mode));
  TEST_ASSERT_EQUAL_UINT(1, next.page_index);
}

void test_key_returns_home_after_last_deck_page() {
  const NavigationState next =
      navigation_after_key_press(Mode::Deck, 2, 3);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Mode::Dashboard),
                          static_cast<uint8_t>(next.mode));
  TEST_ASSERT_EQUAL_UINT(0, next.page_index);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_key_enters_deck_from_dashboard);
  RUN_TEST(test_key_advances_to_next_deck_page);
  RUN_TEST(test_key_returns_home_after_last_deck_page);
  return UNITY_END();
}
