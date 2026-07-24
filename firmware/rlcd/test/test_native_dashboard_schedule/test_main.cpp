#include <unity.h>

#include "render/dashboard_schedule.h"

using namespace rcpaper;

void test_missing_snapshot_requires_sample() {
  TEST_ASSERT_TRUE(dashboard_sample_due(123, 2, false));
}

void test_snapshot_is_reused_within_same_minute() {
  TEST_ASSERT_FALSE(dashboard_sample_due(179, 2, true));
}

void test_next_minute_requires_new_sample() {
  TEST_ASSERT_TRUE(dashboard_sample_due(180, 2, true));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_missing_snapshot_requires_sample);
  RUN_TEST(test_snapshot_is_reused_within_same_minute);
  RUN_TEST(test_next_minute_requires_new_sample);
  return UNITY_END();
}
