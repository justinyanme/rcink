#include <unity.h>
#include <cstdint>
#include "power/refresh_schedule.h"

using namespace rcpaper;

void test_schedules_deadline_from_now() {
  TEST_ASSERT_EQUAL_UINT32(1810000, schedule_next_refresh_ms(10000, 1800));
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
  RUN_TEST(test_due_only_at_or_after_deadline);
  RUN_TEST(test_due_handles_millis_wraparound);
  return UNITY_END();
}
