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

void test_chord_redraws_and_key_long_routes_by_page() {
  buttons::Events chord = {};
  chord.activity = true;
  chord.chord = true;
  TEST_ASSERT_TRUE(interaction_changes_visible_state(chord, false));

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(KeyLongAction::CycleHeatmapWindow),
      static_cast<uint8_t>(
          key_long_action_for_page(View::Deck, "desk-station", true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(KeyLongAction::CycleHeatmapWindow),
      static_cast<uint8_t>(
          key_long_action_for_page(View::Deck, kMonumentPageId, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(KeyLongAction::ToggleCarouselPin),
      static_cast<uint8_t>(
          key_long_action_for_page(View::Deck, "usage-limits", true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(KeyLongAction::ToggleCarouselPin),
      static_cast<uint8_t>(
          key_long_action_for_page(View::Deck, "token-usage", true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(KeyLongAction::ToggleCarouselPin),
      static_cast<uint8_t>(
          key_long_action_for_page(View::Dashboard, kMonumentPageId, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(KeyLongAction::ToggleCarouselPin),
      static_cast<uint8_t>(
          key_long_action_for_page(View::Deck, kMonumentPageId, false)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_consumed_wake_release_does_not_redraw);
  RUN_TEST(test_navigation_or_runtime_change_redraws);
  RUN_TEST(test_chord_redraws_and_key_long_routes_by_page);
  return UNITY_END();
}
