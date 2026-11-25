#include <unity.h>

#include "input/button_gesture.h"
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

void test_usage_defaults_once_then_restores_climate_across_restart_and_wake() {
  const uint8_t startup = startup_view_id(0, true, false);
  TEST_ASSERT_EQUAL_UINT8(1, startup);
  TEST_ASSERT_EQUAL_UINT8(1, startup_view_id(startup, true, true));
  const NavigationState climate = navigation_after_next_press(View::Deck, 0, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Dashboard),
                          static_cast<uint8_t>(climate.view));
  // Cold restarts and timer/KEY wakes honor a saved climate choice.
  TEST_ASSERT_EQUAL_UINT8(0, startup_view_id(0, true, true));
  const NavigationState usage = navigation_after_next_press(climate.view, 0, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck),
                          static_cast<uint8_t>(usage.view));
  const NavigationState back = navigation_after_previous_press(usage.view, 0, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Dashboard),
                          static_cast<uint8_t>(back.view));
}

void test_two_page_usage_ring_and_boot_sync_in_place() {
  NavigationState state = {View::Deck, 0};
  state = navigation_after_next_press(state.view, state.page_index, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck), static_cast<uint8_t>(state.view));
  TEST_ASSERT_EQUAL_UINT(1, state.page_index);  // Allowance -> Token
  state = navigation_after_next_press(state.view, state.page_index, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Dashboard), static_cast<uint8_t>(state.view));
  state = navigation_after_next_press(state.view, state.page_index, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck), static_cast<uint8_t>(state.view));
  TEST_ASSERT_EQUAL_UINT(0, state.page_index);  // climate -> Allowance

  for (size_t page = 0; page < 2; page++) {
    const NavigationState sync = navigation_after_sync_press(View::Deck, page, 2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck), static_cast<uint8_t>(sync.view));
    TEST_ASSERT_EQUAL_UINT(page, sync.page_index);
  }
  const NavigationState dashboard = navigation_after_sync_press(View::Dashboard, 1, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck), static_cast<uint8_t>(dashboard.view));
  TEST_ASSERT_EQUAL_UINT(0, dashboard.page_index);
}

void test_unconfigured_startup_preserves_the_existing_saved_view() {
  TEST_ASSERT_EQUAL_UINT8(0, startup_view_id(0, false, false));
  TEST_ASSERT_EQUAL_UINT8(1, startup_view_id(1, false, false));
  TEST_ASSERT_EQUAL_UINT8(1, startup_view_id(1, false, true));
}

void test_hidden_dashboard_ring_cycles_only_deck_pages_and_forces_deck_startup() {
  TEST_ASSERT_EQUAL_UINT8(1, startup_view_id(0, true, true, false));
  TEST_ASSERT_EQUAL_UINT8(1, startup_view_id(0, true, false, false));

  NavigationState state = navigation_after_next_press(View::Dashboard, 0, 2, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck), static_cast<uint8_t>(state.view));
  TEST_ASSERT_EQUAL_UINT(0, state.page_index);

  state = navigation_after_next_press(state.view, state.page_index, 2, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck), static_cast<uint8_t>(state.view));
  TEST_ASSERT_EQUAL_UINT(1, state.page_index);

  state = navigation_after_next_press(state.view, state.page_index, 2, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck), static_cast<uint8_t>(state.view));
  TEST_ASSERT_EQUAL_UINT(0, state.page_index);

  state = navigation_after_previous_press(state.view, state.page_index, 2, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck), static_cast<uint8_t>(state.view));
  TEST_ASSERT_EQUAL_UINT(1, state.page_index);

  state = navigation_after_previous_press(state.view, state.page_index, 2, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck), static_cast<uint8_t>(state.view));
  TEST_ASSERT_EQUAL_UINT(0, state.page_index);

  state = navigation_after_previous_press(View::Dashboard, 0, 2, false);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(View::Deck), static_cast<uint8_t>(state.view));
  TEST_ASSERT_EQUAL_UINT(1, state.page_index);
}

void test_three_page_hidden_dashboard_ring_and_host_carousel_policy() {
  NavigationState state = {View::Deck, 0};
  state = navigation_after_next_press(state.view, state.page_index, 4, false);
  TEST_ASSERT_EQUAL_UINT(1, state.page_index);
  state = navigation_after_next_press(state.view, state.page_index, 4, false);
  TEST_ASSERT_EQUAL_UINT(2, state.page_index);
  state = navigation_after_next_press(state.view, state.page_index, 4, false);
  TEST_ASSERT_EQUAL_UINT(3, state.page_index);
  state = navigation_after_next_press(state.view, state.page_index, 4, false);
  TEST_ASSERT_EQUAL_UINT(0, state.page_index);

  state = navigation_after_previous_press(state.view, state.page_index, 4, false);
  TEST_ASSERT_EQUAL_UINT(3, state.page_index);

  TEST_ASSERT_FALSE(host_carousel_due(true, false, false, false, 4, 89999, 0));
  TEST_ASSERT_TRUE(host_carousel_due(true, false, false, false, 4, 90000, 0));
  TEST_ASSERT_FALSE(host_carousel_due(false, false, false, false, 4, 90000, 0));
  TEST_ASSERT_FALSE(host_carousel_due(true, true, false, false, 4, 90000, 0));
  TEST_ASSERT_FALSE(host_carousel_due(true, false, true, false, 4, 90000, 0));
  TEST_ASSERT_FALSE(host_carousel_due(true, false, false, true, 4, 90000, 0));
  TEST_ASSERT_FALSE(host_carousel_due(true, false, false, false, 1, 90000, 0));
}

void test_button_gesture_tracker_distinguishes_tap_long_press_and_wake_hold() {
  buttons::ButtonGestureTracker tracker;
  tracker.init(false, false, 0);

  // Press for 250 ms and release -> tap fires after debounce.
  TEST_ASSERT_FALSE(tracker.poll(true, 100).tap);
  TEST_ASSERT_FALSE(tracker.poll(true, 150).long_press);
  const auto short_release = tracker.poll(false, 400);
  TEST_ASSERT_FALSE(short_release.activity);  // waiting for release debounce
  TEST_ASSERT_FALSE(short_release.tap);
  const auto debounced_tap = tracker.poll(false, 445);
  TEST_ASSERT_TRUE(debounced_tap.activity);
  TEST_ASSERT_TRUE(debounced_tap.tap);
  TEST_ASSERT_FALSE(debounced_tap.long_press);

  // Hold >= 600 ms -> long_press fires once while held; release emits no tap.
  tracker.poll(true, 1000);
  tracker.poll(true, 1045);
  TEST_ASSERT_FALSE(tracker.poll(true, 1600).long_press);
  const auto held_long = tracker.poll(true, 1645);
  TEST_ASSERT_TRUE(held_long.long_press);
  TEST_ASSERT_TRUE(held_long.activity);
  TEST_ASSERT_FALSE(tracker.poll(true, 1800).long_press);
  tracker.poll(false, 1810);
  const auto after_long_release = tracker.poll(false, 1860);
  TEST_ASSERT_FALSE(after_long_release.tap);
  TEST_ASSERT_FALSE(after_long_release.long_press);

  // Woken via EXT1 hold -> suppressed until released.
  tracker.init(true, true, 0);
  TEST_ASSERT_FALSE(tracker.poll(true, 700).long_press);
  tracker.poll(false, 710);
  const auto wake_release = tracker.poll(false, 760);
  TEST_ASSERT_FALSE(wake_release.tap);
  TEST_ASSERT_FALSE(wake_release.long_press);
}

void test_dual_button_chord_suppresses_taps_and_long_presses_until_both_release() {
  buttons::DualButtonGestureTracker dual;
  dual.init(false, false, false, 0);

  // Staggered press: KEY down first, BOOT down 100 ms later -> single chord.
  TEST_ASSERT_FALSE(dual.poll(false, true, 100).chord);
  TEST_ASSERT_FALSE(dual.poll(false, true, 140).chord);   // KEY stably down
  TEST_ASSERT_FALSE(dual.poll(true, true, 200).chord);    // BOOT raw down
  const auto chord = dual.poll(true, true, 240);          // BOOT stably down
  TEST_ASSERT_TRUE(chord.chord);
  TEST_ASSERT_TRUE(chord.activity);
  TEST_ASSERT_FALSE(chord.key_tap);
  TEST_ASSERT_FALSE(chord.boot_tap);
  TEST_ASSERT_FALSE(chord.key_long);
  TEST_ASSERT_FALSE(chord.boot_long);

  // Holding both past 600 ms emits nothing.
  const auto held = dual.poll(true, true, 1000);
  TEST_ASSERT_FALSE(held.chord);
  TEST_ASSERT_FALSE(held.key_long);
  TEST_ASSERT_FALSE(held.boot_long);

  // Release KEY first -> no stray key_tap; re-pressing KEY before BOOT releases
  // must NOT re-emit chord or fire key_tap.
  dual.poll(true, false, 1050);
  const auto key_up = dual.poll(true, false, 1090);
  TEST_ASSERT_TRUE(key_up.activity);
  TEST_ASSERT_FALSE(key_up.key_tap);
  TEST_ASSERT_FALSE(key_up.boot_tap);
  dual.poll(true, true, 1120);
  const auto bounce = dual.poll(true, true, 1160);
  TEST_ASSERT_FALSE(bounce.chord);
  dual.poll(true, false, 1200);
  TEST_ASSERT_FALSE(dual.poll(true, false, 1240).key_tap);

  // Release BOOT -> no stray boot_tap; chord latch now clears.
  dual.poll(false, false, 1300);
  const auto boot_up = dual.poll(false, false, 1340);
  TEST_ASSERT_TRUE(boot_up.activity);
  TEST_ASSERT_FALSE(boot_up.boot_tap);
  TEST_ASSERT_FALSE(boot_up.key_tap);
  TEST_ASSERT_FALSE(boot_up.chord);

  // Subsequent normal KEY tap works immediately.
  dual.poll(false, true, 1500);
  dual.poll(false, true, 1540);
  dual.poll(false, false, 1650);
  const auto next_tap = dual.poll(false, false, 1690);
  TEST_ASSERT_TRUE(next_tap.key_tap);
  TEST_ASSERT_FALSE(next_tap.chord);

  // Reverse order (BOOT first, then KEY) also emits chord cleanly.
  dual.poll(true, false, 2000);
  dual.poll(true, false, 2040);
  dual.poll(true, true, 2080);
  const auto rev_chord = dual.poll(true, true, 2120);
  TEST_ASSERT_TRUE(rev_chord.chord);
  dual.poll(false, false, 2200);
  const auto both_up = dual.poll(false, false, 2240);
  TEST_ASSERT_FALSE(both_up.boot_tap);
  TEST_ASSERT_FALSE(both_up.key_tap);

  // Pressing second button AFTER first already fired long_press must NOT chord.
  dual.poll(false, true, 3000);
  dual.poll(false, true, 3040);
  TEST_ASSERT_TRUE(dual.poll(false, true, 3640).key_long);
  dual.poll(true, true, 3700);
  const auto late_boot = dual.poll(true, true, 3740);
  TEST_ASSERT_FALSE(late_boot.chord);
}

void test_eco_key_wake_classification_and_duration_measurement() {
  // Wake classification at setup():
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(buttons::WakeKeyDisposition::None),
      static_cast<uint8_t>(buttons::classify_key_wake(false, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(buttons::WakeKeyDisposition::None),
      static_cast<uint8_t>(buttons::classify_key_wake(false, true)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(buttons::WakeKeyDisposition::ImmediateTap),
      static_cast<uint8_t>(buttons::classify_key_wake(true, false)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(buttons::WakeKeyDisposition::TrackHold),
      static_cast<uint8_t>(buttons::classify_key_wake(true, true)));

  // Active wake press released before threshold -> key_tap.
  buttons::DualButtonGestureTracker wake_tap;
  wake_tap.init(false, true, false, 250, buttons::kWakePressPreElapsedMs);
  wake_tap.poll(false, false, 400);
  const auto short_wake = wake_tap.poll(false, false, 440);
  TEST_ASSERT_TRUE(short_wake.key_tap);
  TEST_ASSERT_FALSE(short_wake.key_long);

  // Active wake press held across threshold (even when now_ms < pre_elapsed) ->
  // key_long in place, and release emits no stray key_tap.
  buttons::DualButtonGestureTracker wake_hold;
  wake_hold.init(false, true, false, 50, buttons::kWakePressPreElapsedMs);
  TEST_ASSERT_FALSE(wake_hold.poll(false, true, 399).key_long);
  const auto long_wake = wake_hold.poll(false, true, 400);
  TEST_ASSERT_TRUE(long_wake.key_long);
  TEST_ASSERT_TRUE(long_wake.activity);
  TEST_ASSERT_FALSE(long_wake.key_tap);
  wake_hold.poll(false, false, 500);
  const auto after_wake_hold = wake_hold.poll(false, false, 540);
  TEST_ASSERT_FALSE(after_wake_hold.key_tap);
  TEST_ASSERT_FALSE(after_wake_hold.key_long);
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
  RUN_TEST(test_usage_defaults_once_then_restores_climate_across_restart_and_wake);
  RUN_TEST(test_two_page_usage_ring_and_boot_sync_in_place);
  RUN_TEST(test_unconfigured_startup_preserves_the_existing_saved_view);
  RUN_TEST(test_hidden_dashboard_ring_cycles_only_deck_pages_and_forces_deck_startup);
  RUN_TEST(test_three_page_hidden_dashboard_ring_and_host_carousel_policy);
  RUN_TEST(test_button_gesture_tracker_distinguishes_tap_long_press_and_wake_hold);
  RUN_TEST(test_dual_button_chord_suppresses_taps_and_long_presses_until_both_release);
  RUN_TEST(test_eco_key_wake_classification_and_duration_measurement);
  return UNITY_END();
}
