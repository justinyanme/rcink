#include <unity.h>

#include "power/network_policy.h"
#include "power/refresh_schedule.h"

using namespace rcpaper;

void test_host_uses_fixed_interval_for_success_and_failure() {
  TEST_ASSERT_EQUAL_UINT32(
      900, network_refresh_interval_sec(RuntimeMode::Host, 7200));
  TEST_ASSERT_EQUAL_UINT32(
      900, network_failure_retry_sec(RuntimeMode::Host, 7200));
}

void test_battery_modes_keep_adaptive_refresh_interval() {
  TEST_ASSERT_EQUAL_UINT32(
      7200, network_refresh_interval_sec(RuntimeMode::Active, 7200));
  TEST_ASSERT_EQUAL_UINT32(
      7200, network_refresh_interval_sec(RuntimeMode::Eco, 7200));
  TEST_ASSERT_EQUAL_UINT32(
      7200, network_failure_retry_sec(RuntimeMode::Active, 7200));
  TEST_ASSERT_EQUAL_UINT32(
      7200, network_failure_retry_sec(RuntimeMode::Eco, 7200));
}

void test_cache_age_and_retry_cooldown_both_gate_fetches() {
  NetworkRefreshQuery query = {};
  query.now_s = 1899;
  query.last_fetch_s = 1000;
  query.clock_valid = true;
  query.interval_sec = 900;
  TEST_ASSERT_FALSE(network_refresh_due(query));
  query.now_s = 1900;
  TEST_ASSERT_TRUE(network_refresh_due(query));
  query.retry_not_before_s = 1960;
  TEST_ASSERT_FALSE(network_refresh_due(query));
  query.now_s = 1960;
  TEST_ASSERT_TRUE(network_refresh_due(query));
}

void test_empty_cache_or_invalid_clock_fetches_after_cooldown() {
  NetworkRefreshQuery query = {};
  query.now_s = 100;
  query.last_fetch_s = 99;
  query.cache_empty = true;
  query.clock_valid = true;
  query.interval_sec = 900;
  TEST_ASSERT_TRUE(network_refresh_due(query));
  query.cache_empty = false;
  query.clock_valid = false;
  TEST_ASSERT_TRUE(network_refresh_due(query));
  query.retry_not_before_s = 101;
  TEST_ASSERT_FALSE(network_refresh_due(query));
}

void test_usage_host_fetches_every_five_minutes_and_retries_a_failure_after_one() {
  NetworkRefreshQuery query = {};
  query.now_s = 1299;
  query.last_fetch_s = 1000;
  query.clock_valid = true;
  query.interval_sec = network_refresh_interval_sec(RuntimeMode::Host, 7200, true);
  TEST_ASSERT_FALSE(network_refresh_due(query));
  query.now_s = 1300;
  TEST_ASSERT_TRUE(network_refresh_due(query));
  // A failed request keeps the last success; a powered host retries a minute later
  // instead of leaving expired numbers up for a full poll.
  TEST_ASSERT_EQUAL_UINT32(60, network_failure_retry_sec(RuntimeMode::Host, 7200, true));
  query.retry_not_before_s = 1300 + network_failure_retry_sec(RuntimeMode::Host, 7200, true);
  query.now_s = 1359;
  TEST_ASSERT_FALSE(network_refresh_due(query));
  query.now_s = 1360;
  TEST_ASSERT_TRUE(network_refresh_due(query));
}

void test_a_failed_attempt_retries_at_its_gate_even_soon_after_a_success() {
  NetworkRefreshQuery query = {};
  query.last_fetch_s = 1000;  // success
  query.clock_valid = true;
  query.interval_sec = 300;
  query.retry_not_before_s = 1100;  // a failure at 1040 + 60 s; the screen says "retry" then
  query.last_attempt_failed = true;
  query.now_s = 1099;
  TEST_ASSERT_FALSE(network_refresh_due(query));
  query.now_s = 1100;
  TEST_ASSERT_TRUE(network_refresh_due(query));
  query.last_attempt_failed = false;  // after a success the poll interval still applies
  TEST_ASSERT_FALSE(network_refresh_due(query));
}

void test_token_cadence_is_independent_and_battery_never_exceeds_hourly() {
  TEST_ASSERT_TRUE(token_refresh_due(1000, 0, 0, RuntimeMode::Host, false));
  TEST_ASSERT_FALSE(token_refresh_due(1899, 1000, 0, RuntimeMode::Host, false));
  TEST_ASSERT_TRUE(token_refresh_due(1900, 1000, 0, RuntimeMode::Host, false));
  TEST_ASSERT_FALSE(token_refresh_due(4599, 1000, 0, RuntimeMode::Active, false));
  TEST_ASSERT_TRUE(token_refresh_due(4600, 1000, 0, RuntimeMode::Active, false));
  TEST_ASSERT_FALSE(token_refresh_due(4599, 1000, 0, RuntimeMode::Eco, false));
  TEST_ASSERT_TRUE(token_refresh_due(4600, 1000, 0, RuntimeMode::Eco, false));
  TEST_ASSERT_FALSE(token_refresh_due(1059, 1000, 1060, RuntimeMode::Host, true));
  TEST_ASSERT_TRUE(token_refresh_due(1060, 1000, 1060, RuntimeMode::Host, true));
  TEST_ASSERT_FALSE(token_refresh_due(1100, 1000, 4600, RuntimeMode::Eco, true));
  TEST_ASSERT_TRUE(token_refresh_due(4600, 1000, 4600, RuntimeMode::Eco, true));
}

void test_token_failure_keeps_allowance_on_its_own_five_minute_gate() {
  // Allowance succeeded while Token failed. Only Allowance controls its gate;
  // Token remains eligible at its own 60-second retry and can ride a later run.
  TEST_ASSERT_EQUAL_UINT32(300, allowance_refresh_cooldown_sec(
      RuntimeMode::Host, 7200, true));
  TEST_ASSERT_TRUE(token_refresh_due(1060, 1000, 1060, RuntimeMode::Host, true));
  TEST_ASSERT_EQUAL_UINT32(60, allowance_refresh_cooldown_sec(
      RuntimeMode::Host, 7200, false));
}

void test_entering_host_clears_a_battery_token_retry() {
  TEST_ASSERT_EQUAL_UINT64(0, token_retry_on_host_entry(4600, true));
  TEST_ASSERT_EQUAL_UINT64(4600, token_retry_on_host_entry(4600, false));
}

void test_usage_does_not_change_battery_low_battery_or_axiom_cadence() {
  const uint32_t low_battery = adaptive_refresh_interval_sec(1800, 2, 19, true);
  for (bool usage : {false, true}) {
    TEST_ASSERT_EQUAL_UINT32(14400, network_refresh_interval_sec(RuntimeMode::Eco, low_battery, usage));
    TEST_ASSERT_EQUAL_UINT32(14400, network_failure_retry_sec(RuntimeMode::Active, low_battery, usage));
    TEST_ASSERT_EQUAL_UINT32(1800, network_refresh_interval_sec(RuntimeMode::Active, 1800, usage));
  }
  TEST_ASSERT_EQUAL_UINT32(900, network_refresh_interval_sec(RuntimeMode::Host, low_battery, false));
  TEST_ASSERT_EQUAL_UINT32(300, network_refresh_interval_sec(RuntimeMode::Host, low_battery, true));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_host_uses_fixed_interval_for_success_and_failure);
  RUN_TEST(test_battery_modes_keep_adaptive_refresh_interval);
  RUN_TEST(test_cache_age_and_retry_cooldown_both_gate_fetches);
  RUN_TEST(test_empty_cache_or_invalid_clock_fetches_after_cooldown);
  RUN_TEST(test_usage_host_fetches_every_five_minutes_and_retries_a_failure_after_one);
  RUN_TEST(test_a_failed_attempt_retries_at_its_gate_even_soon_after_a_success);
  RUN_TEST(test_token_cadence_is_independent_and_battery_never_exceeds_hourly);
  RUN_TEST(test_token_failure_keeps_allowance_on_its_own_five_minute_gate);
  RUN_TEST(test_entering_host_clears_a_battery_token_retry);
  RUN_TEST(test_usage_does_not_change_battery_low_battery_or_axiom_cadence);
  return UNITY_END();
}
