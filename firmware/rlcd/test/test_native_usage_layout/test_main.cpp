#include <unity.h>

#include <cstddef>
#include <cstdint>

#include "render/usage_layout.h"

using namespace rcpaper;

void test_worst_case_usage_rows_have_no_collisions_across_all_counts() {
  // Exact baked Barlow widths from tools/bake_rlcd_fonts.py --metrics:
  // 100 Hero48 = 75, % Name24 = 18; Gemini Name24 (72) + 6 + badge (12) = 90;
  // ~98% to spare Body18 = 112, dry 47h early BodyBold18 = 107, 3h old Chip15 = 47;
  // 6d 23h Body18 = 55.
  const UsageTextWidths worst_verdict = {75, 18, 90, 112, false, 55};
  const UsageTextWidths worst_chip = {50, 18, 90, 47, true, 55};

  for (size_t count : {(size_t)1, (size_t)2, (size_t)3, (size_t)4}) {
    const UsageRowMetrics m = usage_row_metrics(count);
    for (const UsageTextWidths& widths : {worst_verdict, worst_chip}) {
      for (size_t i = 0; i < count; i++) {
        const UsageRowGeometry g = usage_row_geometry(i, widths, count);
        // Horizontal clearances
        TEST_ASSERT_GREATER_OR_EQUAL(6, g.number.left - g.logo.right);
        TEST_ASSERT_GREATER_OR_EQUAL(8, g.road.left - g.number.right);
        TEST_ASSERT_GREATER_OR_EQUAL(8, g.road.left - g.low_box.right);
        TEST_ASSERT_GREATER_OR_EQUAL(8, g.right_label.left - g.name.right);
        TEST_ASSERT_GREATER_OR_EQUAL(8, g.clock_countdown.left - g.pump.right);

        // Vertical clearances
        const int16_t min_car_gap = count >= 4 ? 2 : 6;
        TEST_ASSERT_GREATER_OR_EQUAL(min_car_gap, g.car_bobbing.top - g.name.bottom);
        TEST_ASSERT_GREATER_OR_EQUAL(min_car_gap, g.car_bobbing.top - g.right_label.bottom);
        TEST_ASSERT_LESS_OR_EQUAL(kUsageBodyBottom, g.pace_tick.bottom);
        TEST_ASSERT_LESS_OR_EQUAL(kUsageBodyBottom, g.clock_countdown.bottom);
        TEST_ASSERT_GREATER_OR_EQUAL(kUsageBodyTop, g.name.top);
        TEST_ASSERT_GREATER_OR_EQUAL(kUsageBodyTop, g.low_box.top);
        TEST_ASSERT_LESS_OR_EQUAL(kUsageBodyBottom, g.low_box.bottom);

        const int16_t next_divider = usage_row_top(i, count) + m.pitch;
        TEST_ASSERT_GREATER_OR_EQUAL(4, next_divider - g.pace_tick.bottom);
        TEST_ASSERT_GREATER_OR_EQUAL(4, next_divider - g.clock_countdown.bottom);
      }
    }
  }
}

void test_usage_header_and_footer_clear_chrome_rules() {
  // Header baseline is 19 with max BodyBold18 descender +5 -> bottom 24,
  // clearing the rule at y=28 by 4 px.
  TEST_ASSERT_GREATER_OR_EQUAL(4, kUsageHeaderRuleY - (kUsageHeaderBaseline + 5));

  const UsageFooterWidths worst = {101, 89, 55, 43};  // SYNC FAILED + retry 23:59 + ACTIVE + 100%
  const UsageFooterGeometry footer = usage_footer_geometry(worst);
  TEST_ASSERT_GREATER_OR_EQUAL(4, footer.battery.left - footer.status.right);
  TEST_ASSERT_GREATER_OR_EQUAL(1, footer.status.top - kUsageFooterRuleY);
  TEST_ASSERT_LESS_OR_EQUAL(300, footer.status.bottom);
  TEST_ASSERT_LESS_OR_EQUAL(400, footer.battery.right);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_worst_case_usage_rows_have_no_collisions_across_all_counts);
  RUN_TEST(test_usage_header_and_footer_clear_chrome_rules);
  return UNITY_END();
}
