#include <unity.h>
#include "sense/battery.h"

using namespace rcpaper;

void test_battery_lut_anchors() {
  TEST_ASSERT_EQUAL(0,   battery_mv_to_pct(3000));
  TEST_ASSERT_EQUAL(5,   battery_mv_to_pct(3300));
  TEST_ASSERT_EQUAL(15,  battery_mv_to_pct(3500));
  TEST_ASSERT_EQUAL(30,  battery_mv_to_pct(3650));
  TEST_ASSERT_EQUAL(45,  battery_mv_to_pct(3750));
  TEST_ASSERT_EQUAL(60,  battery_mv_to_pct(3850));
  TEST_ASSERT_EQUAL(75,  battery_mv_to_pct(3950));
  TEST_ASSERT_EQUAL(90,  battery_mv_to_pct(4050));
  TEST_ASSERT_EQUAL(100, battery_mv_to_pct(4200));
}

void test_battery_lut_interpolates() {
  uint8_t mid = battery_mv_to_pct(4000);
  TEST_ASSERT(mid >= 82 && mid <= 83);
}

void test_battery_lut_clamps() {
  TEST_ASSERT_EQUAL(0,   battery_mv_to_pct(2000));
  TEST_ASSERT_EQUAL(100, battery_mv_to_pct(4500));
  TEST_ASSERT_EQUAL(0,   battery_mv_to_pct(0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_battery_lut_anchors);
  RUN_TEST(test_battery_lut_interpolates);
  RUN_TEST(test_battery_lut_clamps);
  return UNITY_END();
}
