#include <unity.h>

#include <cstdint>

#include "power/wake_display_policy.h"

using namespace rcpaper;

void test_short_release_sleeps_only_after_visible_window() {
  WakeDisplayState state = begin_wake_display(1000);

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeDisplayAction::Wait),
      static_cast<uint8_t>(advance_wake_display(state, 1100, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeDisplayAction::Wait),
      static_cast<uint8_t>(advance_wake_display(state, 1200, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeDisplayAction::Wait),
      static_cast<uint8_t>(advance_wake_display(state, 8999, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeDisplayAction::SleepWithButtonWake),
      static_cast<uint8_t>(advance_wake_display(state, 9000, true)));
}

void test_held_button_uses_timer_only_fail_safe() {
  WakeDisplayState state = begin_wake_display(1000);

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeDisplayAction::Wait),
      static_cast<uint8_t>(advance_wake_display(state, 9000, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeDisplayAction::SleepTimerOnly),
      static_cast<uint8_t>(advance_wake_display(state, 31000, false)));
}

void test_bounce_restarts_release_stability_window() {
  WakeDisplayState state = begin_wake_display(0);

  advance_wake_display(state, 7950, true);
  advance_wake_display(state, 7985, false);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeDisplayAction::Wait),
      static_cast<uint8_t>(advance_wake_display(state, 7990, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeDisplayAction::Wait),
      static_cast<uint8_t>(advance_wake_display(state, 8029, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeDisplayAction::SleepWithButtonWake),
      static_cast<uint8_t>(advance_wake_display(state, 8030, true)));
}

void test_visible_deadline_handles_millis_wraparound() {
  constexpr uint32_t start = UINT32_MAX - 3999;
  WakeDisplayState state = begin_wake_display(start);

  advance_wake_display(state, start, true);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeDisplayAction::Wait),
      static_cast<uint8_t>(advance_wake_display(state, 3999, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeDisplayAction::SleepWithButtonWake),
      static_cast<uint8_t>(advance_wake_display(state, 4000, true)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_short_release_sleeps_only_after_visible_window);
  RUN_TEST(test_held_button_uses_timer_only_fail_safe);
  RUN_TEST(test_bounce_restarts_release_stability_window);
  RUN_TEST(test_visible_deadline_handles_millis_wraparound);
  return UNITY_END();
}
