#include <unity.h>

#include "input/navigation.h"

using namespace rcpaper;

void test_key_enters_deck_from_dashboard() {
  const NavigationState next =
      navigation_after_next_press(View::Dashboard, 2, 3);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck),
                          static_cast<uint8_t>(next.view));
  TEST_ASSERT_EQUAL_UINT(0, next.page_index);
}

void test_key_advances_to_next_deck_page() {
  const NavigationState next =
      navigation_after_next_press(View::Deck, 0, 3);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck),
                          static_cast<uint8_t>(next.view));
  TEST_ASSERT_EQUAL_UINT(1, next.page_index);
}

void test_key_returns_home_after_last_deck_page() {
  const NavigationState next =
      navigation_after_next_press(View::Deck, 2, 3);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Dashboard),
                          static_cast<uint8_t>(next.view));
  TEST_ASSERT_EQUAL_UINT(0, next.page_index);
}

void test_boot_enters_last_page_from_dashboard() {
  const NavigationState previous =
      navigation_after_previous_press(View::Dashboard, 0, 3);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck),
                          static_cast<uint8_t>(previous.view));
  TEST_ASSERT_EQUAL_UINT(2, previous.page_index);
}

void test_boot_moves_to_previous_deck_page() {
  const NavigationState previous =
      navigation_after_previous_press(View::Deck, 2, 3);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck),
                          static_cast<uint8_t>(previous.view));
  TEST_ASSERT_EQUAL_UINT(1, previous.page_index);
}

void test_boot_returns_home_before_first_deck_page() {
  const NavigationState previous =
      navigation_after_previous_press(View::Deck, 0, 3);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Dashboard),
                          static_cast<uint8_t>(previous.view));
  TEST_ASSERT_EQUAL_UINT(0, previous.page_index);
}

void test_empty_deck_always_returns_dashboard() {
  const NavigationState next =
      navigation_after_next_press(View::Deck, 99, 0);
  const NavigationState previous =
      navigation_after_previous_press(View::Dashboard, 99, 0);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Dashboard),
                          static_cast<uint8_t>(next.view));
  TEST_ASSERT_EQUAL_UINT(0, next.page_index);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Dashboard),
                          static_cast<uint8_t>(previous.view));
  TEST_ASSERT_EQUAL_UINT(0, previous.page_index);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_key_enters_deck_from_dashboard);
  RUN_TEST(test_key_advances_to_next_deck_page);
  RUN_TEST(test_key_returns_home_after_last_deck_page);
  RUN_TEST(test_boot_enters_last_page_from_dashboard);
  RUN_TEST(test_boot_moves_to_previous_deck_page);
  RUN_TEST(test_boot_returns_home_before_first_deck_page);
  RUN_TEST(test_empty_deck_always_returns_dashboard);
  return UNITY_END();
}
