#include <unity.h>

#include "power/wake_schedule.h"

using namespace rcpaper;

void test_probe_wake_uses_strict_next_30_second_slot() {
  TEST_ASSERT_EQUAL_UINT64(30, next_probe_wake_epoch(0));
  TEST_ASSERT_EQUAL_UINT64(120, next_probe_wake_epoch(90));
  TEST_ASSERT_EQUAL_UINT64(120, next_probe_wake_epoch(91));
  TEST_ASSERT_EQUAL_UINT64(120, next_probe_wake_epoch(119));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_probe_wake_uses_strict_next_30_second_slot);
  return UNITY_END();
}
