#include <unity.h>

#include <array>
#include <cstddef>

#include "render/station_layout.h"

using namespace rcpaper;

namespace {

bool overlaps(const StationBox& a, const StationBox& b) {
  return a.left < b.right && b.left < a.right &&
         a.top < b.bottom && b.top < a.bottom;
}

}  // namespace

void test_station_hero_and_heatmap_spine_have_zero_collisions() {
  // Worst-case baked Barlow widths from src/render/rlcd_fonts.cpp:
  // clock_hero="23:59" (115), eco_caption="LAST SYNC" (84),
  // eco_clock="23:59" (87), odo_hero="99.9M" (118), today_word="today" (44),
  // today_val="99.9M" (48), streak="365d" (40), win_chip="6M" (22),
  // scale_label="7D" (20).
  const StationHeroWidths worst = {115, 84, 87, 118, 44, 48, 40, 22, 20};
  const StationSpineGeometry g = station_spine_geometry(worst);

  // Horizontal separation across Hero in both HOST/ACTIVE and ECO modes.
  TEST_ASSERT_GREATER_OR_EQUAL(10, kStationHeroSepX - g.clock_hero.right);
  TEST_ASSERT_GREATER_OR_EQUAL(10, kStationHeroSepX - g.eco_caption.right);
  TEST_ASSERT_GREATER_OR_EQUAL(10, kStationHeroSepX - g.eco_clock.right);
  TEST_ASSERT_GREATER_OR_EQUAL(10, g.odo_hero.left - kStationHeroSepX);
  TEST_ASSERT_GREATER_OR_EQUAL(16, g.today_line.left - g.odo_hero.right);
  TEST_ASSERT_GREATER_OR_EQUAL(16, g.streak_line.left - g.odo_hero.right);

  // Vertical rhythm through Hero -> Months -> Grid -> 7D scale label -> Rows.
  TEST_ASSERT_GREATER_OR_EQUAL(4, g.eco_clock.top - g.eco_caption.bottom);
  TEST_ASSERT_GREATER_OR_EQUAL(5, g.streak_line.top - g.today_line.bottom);
  TEST_ASSERT_GREATER_OR_EQUAL(4, kStationHeroDividerY - g.clock_hero.bottom);
  TEST_ASSERT_GREATER_OR_EQUAL(3, g.months.top - kStationHeroDividerY);
  TEST_ASSERT_GREATER_OR_EQUAL(3, g.grid.top - g.months.bottom);
  TEST_ASSERT_GREATER_OR_EQUAL(5, g.scale_label.top - g.grid.bottom);
  TEST_ASSERT_LESS_OR_EQUAL(kStationRowsTop, g.scale_label.bottom);

  const std::array<StationBox, 7> host_boxes = {
      g.clock_hero,  g.odo_hero, g.today_line,  g.streak_line,
      g.months,      g.grid,     g.scale_label,
  };
  for (size_t i = 0; i < host_boxes.size(); i++) {
    TEST_ASSERT_GREATER_OR_EQUAL(0, host_boxes[i].left);
    TEST_ASSERT_LESS_OR_EQUAL(400, host_boxes[i].right);
    for (size_t j = i + 1; j < host_boxes.size(); j++) {
      TEST_ASSERT_FALSE(overlaps(host_boxes[i], host_boxes[j]));
    }
  }
  TEST_ASSERT_FALSE(overlaps(g.eco_caption, g.eco_clock));
}

void test_station_provider_rows_clear_all_columns_across_1_to_4_rows() {
  // Worst-case row cases:
  // A: Gemini + Antigravity (56 + 4 + 12 = 72), 100% (44), 6d 23h (55), 99.9M (48)
  // B: Claude (56), low-fuel [8%] (24 + pill), [23h old] chip (57 + 7), 999M (43)
  const StationRowWidths worst_normal = {72, 44, false, 55, false, 48};
  const StationRowWidths worst_chips = {72, 24, true, 57, true, 48};

  for (size_t count : {(size_t)1, (size_t)2, (size_t)3, (size_t)4}) {
    for (size_t i = 0; i < count; i++) {
      for (const StationRowWidths& w : {worst_normal, worst_chips}) {
        const StationRowGeometry r = station_row_geometry(i, count, w);
        TEST_ASSERT_GREATER_OR_EQUAL(10, r.pct.left - r.identity.right);
        TEST_ASSERT_GREATER_OR_EQUAL(6, r.bar.left - r.pct.right);
        TEST_ASSERT_GREATER_OR_EQUAL(6, r.countdown.left - r.bar.right);
        TEST_ASSERT_GREATER_OR_EQUAL(10, kStationColSepX - r.countdown.right);
        TEST_ASSERT_GREATER_OR_EQUAL(10, r.token.left - kStationColSepX);

        const std::array<StationBox, 5> cols = {
            r.identity, r.pct, r.bar, r.countdown, r.token,
        };
        for (size_t a = 0; a < cols.size(); a++) {
          TEST_ASSERT_GREATER_OR_EQUAL(kStationRowsTop, cols[a].top);
          TEST_ASSERT_LESS_OR_EQUAL(kStationFooterRuleY, cols[a].bottom);
          for (size_t b = a + 1; b < cols.size(); b++) {
            TEST_ASSERT_FALSE(overlaps(cols[a], cols[b]));
          }
        }
      }
      if (i + 1 < count) {
        const StationRowGeometry cur =
            station_row_geometry(i, count, worst_chips);
        const StationRowGeometry nxt =
            station_row_geometry(i + 1, count, worst_chips);
        TEST_ASSERT_GREATER_OR_EQUAL(8, nxt.pct.top - cur.pct.bottom);
      }
    }
  }
}

void test_station_dual_source_footers_clear_battery_and_pinned_chip() {
  // Worst-case runtime_chip = PINNED (57), battery = 100% (43).
  // Chips: SYNC FAILED (101), TOKEN FAIL (~94), ALLOWANCE (~88), OLD DATA (76).
  // Texts: retry 23:59 (89), checked 23:59 (116), M4 23:59 (74).
  for (const StationFooterWidths& fw : {
           StationFooterWidths{101, 89, 57, 43},
           StationFooterWidths{96, 89, 57, 43},
           StationFooterWidths{90, 89, 57, 43},
           StationFooterWidths{76, 116, 57, 43},
           StationFooterWidths{76, 74, 57, 43},
       }) {
    const StationFooterGeometry g = station_footer_geometry(fw);
    TEST_ASSERT_GREATER_OR_EQUAL(16, g.battery.left - g.status.right);
    TEST_ASSERT_LESS_OR_EQUAL(400, g.battery.right);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_station_hero_and_heatmap_spine_have_zero_collisions);
  RUN_TEST(test_station_provider_rows_clear_all_columns_across_1_to_4_rows);
  RUN_TEST(test_station_dual_source_footers_clear_battery_and_pinned_chip);
  return UNITY_END();
}
