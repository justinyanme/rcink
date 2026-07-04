#include <unity.h>
#include <cstdint>
#include "power/refresh_schedule.h"

using namespace rcpaper;

void test_schedules_deadline_from_now() {
  TEST_ASSERT_EQUAL_UINT32(1810000, schedule_next_refresh_ms(10000, 1800));
}

void test_adaptive_interval_scales_unchanged_fetches_and_low_battery() {
  TEST_ASSERT_EQUAL_UINT32(1800, adaptive_refresh_interval_sec(1800, 0, 80, true));
  TEST_ASSERT_EQUAL_UINT32(3600, adaptive_refresh_interval_sec(1800, 1, 80, true));
  TEST_ASSERT_EQUAL_UINT32(7200, adaptive_refresh_interval_sec(1800, 2, 80, true));
  TEST_ASSERT_EQUAL_UINT32(7200, adaptive_refresh_interval_sec(1800, 99, 80, true));
  TEST_ASSERT_EQUAL_UINT32(14400, adaptive_refresh_interval_sec(1800, 2, 19, true));
  TEST_ASSERT_EQUAL_UINT32(7200, adaptive_refresh_interval_sec(1800, 2, 0, false));
}

void test_record_successful_fetch_tracks_unchanged_streak() {
  DeviceState state;
  std::vector<PageRecord> pages = {PageRecord{"overview", 5, 100, "{}"}};

  record_successful_fetch(state, pages);
  TEST_ASSERT_NOT_EQUAL(0, state.last_fetch_fingerprint);
  TEST_ASSERT_EQUAL_UINT8(0, state.unchanged_fetches);

  record_successful_fetch(state, pages);
  TEST_ASSERT_EQUAL_UINT8(1, state.unchanged_fetches);

  pages[0].data_ts = 101;
  record_successful_fetch(state, pages);
  TEST_ASSERT_EQUAL_UINT8(0, state.unchanged_fetches);
}

void test_due_only_at_or_after_deadline() {
  const uint32_t deadline = schedule_next_refresh_ms(10000, 1800);

  TEST_ASSERT_FALSE(refresh_due_ms(deadline - 1, deadline));
  TEST_ASSERT_TRUE(refresh_due_ms(deadline, deadline));
  TEST_ASSERT_TRUE(refresh_due_ms(deadline + 1, deadline));
}

void test_due_handles_millis_wraparound() {
  const uint32_t deadline = schedule_next_refresh_ms(UINT32_MAX - 999, 2);

  TEST_ASSERT_EQUAL_UINT32(1000, deadline);
  TEST_ASSERT_FALSE(refresh_due_ms(UINT32_MAX - 5, deadline));
  TEST_ASSERT_FALSE(refresh_due_ms(999, deadline));
  TEST_ASSERT_TRUE(refresh_due_ms(1000, deadline));
  TEST_ASSERT_TRUE(refresh_due_ms(1001, deadline));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_schedules_deadline_from_now);
  RUN_TEST(test_adaptive_interval_scales_unchanged_fetches_and_low_battery);
  RUN_TEST(test_record_successful_fetch_tracks_unchanged_streak);
  RUN_TEST(test_due_only_at_or_after_deadline);
  RUN_TEST(test_due_handles_millis_wraparound);
  return UNITY_END();
}
