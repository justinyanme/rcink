#include <unity.h>

#include "input/interaction_policy.h"

using namespace rcpaper;

void test_consumed_wake_release_does_not_redraw() {
  const buttons::Events consumed_release = {
      true,
      false,
      false,
  };

  TEST_ASSERT_FALSE(
      interaction_changes_visible_state(consumed_release, false));
}

void test_navigation_or_runtime_change_redraws() {
  const buttons::Events key_tap = {
      true,
      false,
      true,
  };
  const buttons::Events no_button = {};

  TEST_ASSERT_TRUE(interaction_changes_visible_state(key_tap, false));
  TEST_ASSERT_TRUE(interaction_changes_visible_state(no_button, true));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_consumed_wake_release_does_not_redraw);
  RUN_TEST(test_navigation_or_runtime_change_redraws);
  return UNITY_END();
}
