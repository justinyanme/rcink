#include <unity.h>

#include "power/network_policy.h"

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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_host_uses_fixed_interval_for_success_and_failure);
  RUN_TEST(test_battery_modes_keep_adaptive_refresh_interval);
  RUN_TEST(test_cache_age_and_retry_cooldown_both_gate_fetches);
  RUN_TEST(test_empty_cache_or_invalid_clock_fetches_after_cooldown);
  return UNITY_END();
}
