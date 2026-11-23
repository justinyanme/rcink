#include <unity.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "render/link_status.h"
#include "render/token_layout.h"

using namespace rcpaper;

int vertical_gap(const TokenBox& upper, const TokenBox& lower) {
  return lower.top - upper.bottom;
}

bool overlaps(const TokenBox& a, const TokenBox& b) {
  return a.left < b.right && b.left < a.right &&
         a.top < b.bottom && b.top < a.bottom;
}

void test_worst_case_token_rows_have_real_glyph_and_element_gaps() {
  // Exact baked Barlow widths from tools/bake_rlcd_fonts.py --metrics:
  // Gemini Name24 (72) + 6 + Antigravity (12) = 90;
  // widest 99.9M/9.99M: Hero36 = 89 (3 rows), Hero48 = 118 (1-2 rows);
  // widest today line: today 999M (92) + symmetric dot (12) + 999%+ of a usual day (166) = 270;
  // ODO (33) + 6 + 999B (41) = 80.
  for (size_t count : {(size_t)1, (size_t)2, (size_t)3}) {
    const int16_t total_w = count >= 3 ? 89 : 118;
    const TokenTextWidths worst = {90, total_w, 270, 80};
    const TokenRowMetrics m = token_row_metrics(count);
    for (size_t i = 0; i < count; i++) {
      const TokenRowGeometry row = token_row_geometry(i, worst, count);
      TEST_ASSERT_GREATER_OR_EQUAL(4, vertical_gap(row.provider, row.total));
      TEST_ASSERT_GREATER_OR_EQUAL(4, vertical_gap(row.seven_days, row.total));
      TEST_ASSERT_GREATER_OR_EQUAL(4, vertical_gap(row.odometer, row.chart));
      TEST_ASSERT_GREATER_OR_EQUAL(4, vertical_gap(row.chart, row.today));
      TEST_ASSERT_GREATER_OR_EQUAL(4, vertical_gap(row.total, row.today));
      TEST_ASSERT_GREATER_OR_EQUAL(10, row.odometer.left - row.seven_days.right);
      TEST_ASSERT_GREATER_OR_EQUAL(10, row.chart.left - row.total.right);

      const std::array<TokenBox, 6> boxes = {
          row.provider, row.seven_days, row.total, row.chart, row.today, row.odometer};
      for (size_t a = 0; a < boxes.size(); a++) {
        TEST_ASSERT_GREATER_OR_EQUAL(0, boxes[a].left);
        TEST_ASSERT_LESS_OR_EQUAL(400, boxes[a].right);
        TEST_ASSERT_GREATER_OR_EQUAL(kTokenBodyTop, boxes[a].top);
        TEST_ASSERT_LESS_OR_EQUAL(kTokenBodyBottom, boxes[a].bottom);
        for (size_t b = a + 1; b < boxes.size(); b++) {
          TEST_ASSERT_FALSE(overlaps(boxes[a], boxes[b]));
        }
      }
      const int divider_y = token_row_top(i, count) + m.pitch;
      TEST_ASSERT_GREATER_OR_EQUAL(4, divider_y - row.today.bottom);
    }
  }
}

void test_footer_worst_cases_clear_the_battery_and_runtime_block() {
  // Exact baked Barlow widths: SYNC FAILED (101), retry 23:59 (89), ACTIVE (55), 100% (43).
  const TokenFooterWidths failure = {101, 89, 55, 43};
  const TokenFooterGeometry failed = token_footer_geometry(failure);
  TEST_ASSERT_GREATER_OR_EQUAL(4, failed.battery.left - failed.status.right);

  const TokenFooterGeometry old = token_footer_geometry({76, 116, 55, 43});
  TEST_ASSERT_GREATER_OR_EQUAL(4, old.battery.left - old.status.right);
  const TokenFooterGeometry syncing = token_footer_geometry({66, 103, 55, 43});
  TEST_ASSERT_GREATER_OR_EQUAL(4, syncing.battery.left - syncing.status.right);

  for (int16_t runtime : {39, 55, 30}) {  // HOST, ACTIVE, ECO
    const TokenFooterGeometry each = token_footer_geometry({101, 89, runtime, 43});
    TEST_ASSERT_GREATER_OR_EQUAL(4, each.battery.left - each.status.right);
    TEST_ASSERT_LESS_OR_EQUAL(400, each.battery.right);
  }

  for (const TokenFooterWidths& link_footer : {
           TokenFooterWidths{60, 89, 55, 43},
           TokenFooterWidths{60, 88, 55, 43},
           TokenFooterWidths{56, 88, 55, 43},
       }) {
    const TokenFooterGeometry geom = token_footer_geometry(link_footer);
    TEST_ASSERT_GREATER_OR_EQUAL(4, geom.battery.left - geom.status.right);
  }
}

void test_lifetime_origin_label_clears_every_headline() {
  // Busiest this week: Gemini (213) vs since Sep '25 (104).
  const TokenHeadlineGeometry widest = token_headline_geometry(213, 104);
  TEST_ASSERT_GREATER_OR_EQUAL(4, widest.since.left - widest.headline.right);
  const TokenHeadlineGeometry tear = token_headline_geometry(199, 104);
  TEST_ASSERT_GREATER_OR_EQUAL(4, tear.since.left - tear.headline.right);

  for (const std::string& link_headline : {
           wifi_failed_headline("OnePlus 15 EAA8"),
           wifi_failed_headline("WWWWWWWWWWWWWWWWWWWW"),
           wifi_failed_headline("@@@@@@@@@@@@@@@@@@@@"),
           wifi_failed_headline("iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii"),
           std::string(ntp_failed_headline()),
       }) {
    const int16_t width = (int16_t)freesans9_width(link_headline);
    const TokenHeadlineGeometry geom = token_headline_geometry(width, 104, false);
    TEST_ASSERT_GREATER_OR_EQUAL(4, geom.since.left - geom.headline.right);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_worst_case_token_rows_have_real_glyph_and_element_gaps);
  RUN_TEST(test_footer_worst_cases_clear_the_battery_and_runtime_block);
  RUN_TEST(test_lifetime_origin_label_clears_every_headline);
  return UNITY_END();
}
