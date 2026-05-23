#include <unity.h>
#include "power/nav_delta.h"

using rcpaper::apply_nav_delta;

void test_zero_delta_is_identity() {
  TEST_ASSERT_EQUAL_UINT8(0, apply_nav_delta(0, 0, 5));
  TEST_ASSERT_EQUAL_UINT8(3, apply_nav_delta(3, 0, 5));
}

void test_positive_delta_wraps_forward() {
  // 5 pages, start at 3, +3 → 6 mod 5 = 1
  TEST_ASSERT_EQUAL_UINT8(1, apply_nav_delta(3, 3, 5));
  // +5 = full lap → same
  TEST_ASSERT_EQUAL_UINT8(2, apply_nav_delta(2, 5, 5));
}

void test_negative_delta_wraps_backward() {
  // 5 pages, start at 1, -3 → -2 → wraps to 3
  TEST_ASSERT_EQUAL_UINT8(3, apply_nav_delta(1, -3, 5));
  // -1 from 0 → wraps to 4
  TEST_ASSERT_EQUAL_UINT8(4, apply_nav_delta(0, -1, 5));
}

void test_mixed_taps_net_result() {
  // Three forward + one back from page 2 in a 7-page set = net +2 → page 4.
  TEST_ASSERT_EQUAL_UINT8(4, apply_nav_delta(2, 2, 7));
  // Three back + one forward from page 1 in a 7-page set = net -2 → page 6.
  TEST_ASSERT_EQUAL_UINT8(6, apply_nav_delta(1, -2, 7));
}

void test_single_page_clamps_to_zero() {
  // Degenerate but legal: one-page state. Any delta returns 0.
  TEST_ASSERT_EQUAL_UINT8(0, apply_nav_delta(0, 5, 1));
  TEST_ASSERT_EQUAL_UINT8(0, apply_nav_delta(0, -7, 1));
}

void test_large_positive_delta() {
  // Hard-cap behavior is enforced by the caller, but the helper itself
  // must still return the correct modulo even for big inputs.
  TEST_ASSERT_EQUAL_UINT8(2, apply_nav_delta(0, 17, 5));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_zero_delta_is_identity);
  RUN_TEST(test_positive_delta_wraps_forward);
  RUN_TEST(test_negative_delta_wraps_backward);
  RUN_TEST(test_mixed_taps_net_result);
  RUN_TEST(test_single_page_clamps_to_zero);
  RUN_TEST(test_large_positive_delta);
  return UNITY_END();
}
