#include <unity.h>

#include "power/runtime_policy.h"

using namespace rcpaper;

static RuntimeBootInput idle_boot_at(uint32_t now_ms) {
  RuntimeBootInput input = {};
  input.now_ms = now_ms;
  return input;
}

static RuntimeBootInput key_wake_at(uint32_t now_ms) {
  RuntimeBootInput input = idle_boot_at(now_ms);
  input.key_wake = true;
  return input;
}

static RuntimeBootInput host_boot_at(uint32_t now_ms) {
  RuntimeBootInput input = idle_boot_at(now_ms);
  input.usb_host_present = true;
  return input;
}

static RuntimeBootInput dev_boot_at(uint32_t now_ms,
                                    bool usb_host_present = false) {
  RuntimeBootInput input = idle_boot_at(now_ms);
  input.always_active = true;
  input.usb_host_present = usb_host_present;
  return input;
}

static RuntimeInput idle_input_at(uint32_t now_ms) {
  RuntimeInput input = {};
  input.now_ms = now_ms;
  return input;
}

static RuntimeInput dev_input_at(uint32_t now_ms) {
  RuntimeInput input = idle_input_at(now_ms);
  input.always_active = true;
  return input;
}

static RuntimeInput host_input_at(uint32_t now_ms) {
  RuntimeInput input = idle_input_at(now_ms);
  input.usb_host_present = true;
  return input;
}

static RuntimeInput activity_input_at(uint32_t now_ms) {
  RuntimeInput input = idle_input_at(now_ms);
  input.user_activity = true;
  return input;
}

static RuntimeInput held_input_at(uint32_t now_ms) {
  RuntimeInput input = idle_input_at(now_ms);
  input.any_button_down = true;
  return input;
}

void test_key_wake_opens_active_window() {
  const RuntimeState state = initial_runtime_state(key_wake_at(1000));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Active),
                          static_cast<uint8_t>(state.mode));
  TEST_ASSERT_EQUAL_UINT32(9000, state.active_deadline_ms);
}

void test_usb_host_starts_in_host_mode() {
  const RuntimeState state = initial_runtime_state(host_boot_at(1000));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Host),
                          static_cast<uint8_t>(state.mode));
}

void test_dev_build_stays_active_without_claiming_usb_host() {
  const RuntimeState active = initial_runtime_state(dev_boot_at(1000));
  const RuntimeDecision later =
      advance_runtime_state(active, dev_input_at(100000));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Active),
                          static_cast<uint8_t>(active.mode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Active),
                          static_cast<uint8_t>(later.state.mode));
  TEST_ASSERT_FALSE(later.sleep_requested);
}

void test_usb_host_promotes_active_session_to_host() {
  const RuntimeState active = initial_runtime_state(key_wake_at(1000));
  const RuntimeInput input = host_input_at(1200);

  const RuntimeDecision decision = advance_runtime_state(active, input);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Host),
                          static_cast<uint8_t>(decision.state.mode));
  TEST_ASSERT_TRUE(decision.mode_changed);
  TEST_ASSERT_FALSE(decision.sleep_requested);
}

void test_host_loss_starts_debounce_without_leaving_host() {
  const RuntimeState host = initial_runtime_state(host_boot_at(0));
  const RuntimeInput input = idle_input_at(100);

  const RuntimeDecision decision = advance_runtime_state(host, input);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Host),
                          static_cast<uint8_t>(decision.state.mode));
  TEST_ASSERT_TRUE(decision.state.host_missing);
  TEST_ASSERT_EQUAL_UINT32(100, decision.state.host_missing_since_ms);
  TEST_ASSERT_FALSE(decision.mode_changed);
}

void test_sustained_host_loss_falls_back_to_active_window() {
  RuntimeState state = initial_runtime_state(host_boot_at(0));
  state = advance_runtime_state(state, idle_input_at(100)).state;

  const RuntimeDecision before = advance_runtime_state(
      state, idle_input_at(1099));
  const RuntimeDecision elapsed = advance_runtime_state(
      before.state, idle_input_at(1100));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Host),
                          static_cast<uint8_t>(before.state.mode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Active),
                          static_cast<uint8_t>(elapsed.state.mode));
  TEST_ASSERT_EQUAL_UINT32(9100, elapsed.state.active_deadline_ms);
  TEST_ASSERT_TRUE(elapsed.mode_changed);
}

void test_host_reconnect_cancels_loss_debounce() {
  RuntimeState state = initial_runtime_state(host_boot_at(0));
  state = advance_runtime_state(state, idle_input_at(100)).state;

  const RuntimeDecision reconnected = advance_runtime_state(
      state, host_input_at(500));
  const RuntimeDecision lost_again = advance_runtime_state(
      reconnected.state, idle_input_at(600));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Host),
                          static_cast<uint8_t>(reconnected.state.mode));
  TEST_ASSERT_FALSE(reconnected.state.host_missing);
  TEST_ASSERT_TRUE(lost_again.state.host_missing);
  TEST_ASSERT_EQUAL_UINT32(600, lost_again.state.host_missing_since_ms);
}

void test_dev_build_keeps_full_host_loss_debounce() {
  RuntimeState state = initial_runtime_state(dev_boot_at(0, true));
  state = advance_runtime_state(state, dev_input_at(100)).state;

  const RuntimeDecision before = advance_runtime_state(
      state, dev_input_at(1099));
  const RuntimeDecision elapsed = advance_runtime_state(
      before.state, dev_input_at(1100));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Host),
                          static_cast<uint8_t>(before.state.mode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Active),
                          static_cast<uint8_t>(elapsed.state.mode));
}

void test_active_window_sleeps_only_at_deadline() {
  const RuntimeState active = initial_runtime_state(key_wake_at(1000));

  const RuntimeDecision before = advance_runtime_state(
      active, idle_input_at(8999));
  const RuntimeDecision elapsed = advance_runtime_state(
      before.state, idle_input_at(9000));
  const RuntimeDecision settled = advance_runtime_state(
      elapsed.state, idle_input_at(9001));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Active),
                          static_cast<uint8_t>(before.state.mode));
  TEST_ASSERT_FALSE(before.sleep_requested);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Eco),
                          static_cast<uint8_t>(elapsed.state.mode));
  TEST_ASSERT_TRUE(elapsed.mode_changed);
  TEST_ASSERT_FALSE(elapsed.sleep_requested);
  TEST_ASSERT_TRUE(settled.sleep_requested);
}

void test_button_activity_restarts_active_window() {
  const RuntimeState active = initial_runtime_state(key_wake_at(1000));

  const RuntimeDecision activity = advance_runtime_state(
      active, activity_input_at(8500));
  const RuntimeDecision old_deadline = advance_runtime_state(
      activity.state, idle_input_at(9000));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Active),
                          static_cast<uint8_t>(activity.state.mode));
  TEST_ASSERT_EQUAL_UINT32(16500, activity.state.active_deadline_ms);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Active),
                          static_cast<uint8_t>(old_deadline.state.mode));
  TEST_ASSERT_FALSE(old_deadline.sleep_requested);
}

void test_active_deadline_handles_millis_wraparound() {
  const RuntimeState active =
      initial_runtime_state(key_wake_at(UINT32_MAX - 3999));

  const RuntimeDecision before = advance_runtime_state(
      active, idle_input_at(3999));
  const RuntimeDecision elapsed = advance_runtime_state(
      before.state, idle_input_at(4000));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Active),
                          static_cast<uint8_t>(before.state.mode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Eco),
                          static_cast<uint8_t>(elapsed.state.mode));
}

void test_held_button_prevents_sleep_and_restarts_window() {
  const RuntimeState active = initial_runtime_state(key_wake_at(1000));

  const RuntimeDecision held = advance_runtime_state(
      active, held_input_at(9000));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Active),
                          static_cast<uint8_t>(held.state.mode));
  TEST_ASSERT_EQUAL_UINT32(17000, held.state.active_deadline_ms);
  TEST_ASSERT_FALSE(held.mode_changed);
  TEST_ASSERT_FALSE(held.sleep_requested);
}

void test_eco_waits_for_all_buttons_to_be_released() {
  const RuntimeState eco = initial_runtime_state(idle_boot_at(1000));

  const RuntimeDecision held = advance_runtime_state(
      eco, held_input_at(1001));
  const RuntimeDecision released = advance_runtime_state(
      held.state, idle_input_at(1002));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Eco),
                          static_cast<uint8_t>(held.state.mode));
  TEST_ASSERT_FALSE(held.sleep_requested);
  TEST_ASSERT_TRUE(released.sleep_requested);
}

void test_activity_while_eco_opens_active_window() {
  const RuntimeState eco = initial_runtime_state(idle_boot_at(1000));

  const RuntimeDecision activity = advance_runtime_state(
      eco, activity_input_at(1200));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeMode::Active),
                          static_cast<uint8_t>(activity.state.mode));
  TEST_ASSERT_EQUAL_UINT32(9200, activity.state.active_deadline_ms);
  TEST_ASSERT_TRUE(activity.mode_changed);
  TEST_ASSERT_FALSE(activity.sleep_requested);
}

void test_runtime_mode_names_match_screen_badges() {
  TEST_ASSERT_EQUAL_STRING("ECO", runtime_mode_name(RuntimeMode::Eco));
  TEST_ASSERT_EQUAL_STRING("ACTIVE", runtime_mode_name(RuntimeMode::Active));
  TEST_ASSERT_EQUAL_STRING("HOST", runtime_mode_name(RuntimeMode::Host));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_key_wake_opens_active_window);
  RUN_TEST(test_usb_host_starts_in_host_mode);
  RUN_TEST(test_dev_build_stays_active_without_claiming_usb_host);
  RUN_TEST(test_usb_host_promotes_active_session_to_host);
  RUN_TEST(test_host_loss_starts_debounce_without_leaving_host);
  RUN_TEST(test_sustained_host_loss_falls_back_to_active_window);
  RUN_TEST(test_host_reconnect_cancels_loss_debounce);
  RUN_TEST(test_dev_build_keeps_full_host_loss_debounce);
  RUN_TEST(test_active_window_sleeps_only_at_deadline);
  RUN_TEST(test_button_activity_restarts_active_window);
  RUN_TEST(test_active_deadline_handles_millis_wraparound);
  RUN_TEST(test_held_button_prevents_sleep_and_restarts_window);
  RUN_TEST(test_eco_waits_for_all_buttons_to_be_released);
  RUN_TEST(test_activity_while_eco_opens_active_window);
  RUN_TEST(test_runtime_mode_names_match_screen_badges);
  return UNITY_END();
}
