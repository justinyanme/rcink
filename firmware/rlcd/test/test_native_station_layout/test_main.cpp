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

void test_station_hero_split_spine_and_skyline_have_zero_collisions() {
  // Worst-case baked Barlow widths from src/render/rlcd_fonts.cpp:
  // clock_hero="23:59" (115), eco_caption="LAST SYNC" (84),
  // eco_clock="23:59" (87), odo_hero="99.9M" (118), today_word="today" (44),
  // today_val="99.9M" (48), streak="365d" (40), win_chip="6M" (22),
  // scale_label="7D" (20), skyline_label="8W" (24), skyline_delta="+999%" (50),
  // masthead_title="STATION" (70), ambient_climate="29.9°C · 99%" (97).
  const StationHeroWidths worst = {115, 84, 87, 118, 44, 48, 40,
                                   22,  20, 24, 50,  70, 97};

  for (size_t bars : {(size_t)8, (size_t)7, (size_t)6}) {
    const StationSpineGeometry g = station_spine_geometry(worst, bars);

    // Horizontal separation across Hero in both HOST/ACTIVE and ECO modes.
    TEST_ASSERT_GREATER_OR_EQUAL(10, kStationHeroSepX - g.clock_hero.right);
    TEST_ASSERT_GREATER_OR_EQUAL(10, kStationHeroSepX - g.eco_caption.right);
    TEST_ASSERT_GREATER_OR_EQUAL(10, kStationHeroSepX - g.eco_clock.right);
    TEST_ASSERT_GREATER_OR_EQUAL(10, g.odo_hero.left - kStationHeroSepX);
    TEST_ASSERT_GREATER_OR_EQUAL(16, g.today_line.left - g.odo_hero.right);
    TEST_ASSERT_GREATER_OR_EQUAL(16, g.streak_line.left - g.odo_hero.right);

    // Split spine horizontal separation: Heatmap | Divider | Token Skyline.
    TEST_ASSERT_EQUAL(kStationGridRight, g.grid.right);
    TEST_ASSERT_GREATER_OR_EQUAL(8, g.spine_divider.left - g.grid.right);
    TEST_ASSERT_GREATER_OR_EQUAL(7, g.skyline_plot.left - g.spine_divider.right);
    TEST_ASSERT_GREATER_OR_EQUAL(16, g.skyline_delta.left - g.skyline_label.right);

    // Vertical rhythm through Hero -> Split Spine -> 7D label -> Rows.
    TEST_ASSERT_GREATER_OR_EQUAL(4, g.eco_clock.top - g.eco_caption.bottom);
    TEST_ASSERT_GREATER_OR_EQUAL(5, g.streak_line.top - g.today_line.bottom);
    TEST_ASSERT_GREATER_OR_EQUAL(4, kStationHeroDividerY - g.clock_hero.bottom);
    TEST_ASSERT_GREATER_OR_EQUAL(1, g.skyline_delta.top - kStationHeroDividerY);
    TEST_ASSERT_GREATER_OR_EQUAL(1, g.skyline_plot.top - g.skyline_delta.bottom);
    TEST_ASSERT_GREATER_OR_EQUAL(3, g.months.top - kStationHeroDividerY);
    TEST_ASSERT_GREATER_OR_EQUAL(3, g.grid.top - g.months.bottom);
    TEST_ASSERT_GREATER_OR_EQUAL(5, g.scale_label.top - g.grid.bottom);
    TEST_ASSERT_LESS_OR_EQUAL(kStationRowsTop, g.scale_label.bottom);

    // Tallest skyscraper + rooftop antenna + beacon stays inside skyline sky box.
    const int16_t max_beacon_top =
        kStationSkylineGroundY - kStationSkylineMaxBarH - kStationSkylineAntennaH;
    TEST_ASSERT_GREATER_OR_EQUAL(kStationSkylineTop, max_beacon_top);

    // Every skyscraper bar stays inside [kStationSkylineX, kStationSkylineRight] with clean gaps.
    const StationSkylineBarMetrics bm = station_skyline_bar_metrics(bars);
    for (size_t i = 0; i < bars; i++) {
      const int16_t bx = station_skyline_bar_x(i, bars);
      TEST_ASSERT_GREATER_OR_EQUAL(kStationSkylineX, bx);
      TEST_ASSERT_LESS_OR_EQUAL(kStationSkylineRight, bx + bm.bar_w);
      if (i > 0) {
        const int16_t prev = station_skyline_bar_x(i - 1, bars);
        TEST_ASSERT_GREATER_OR_EQUAL(3, bx - (prev + bm.bar_w));
      }
    }

    const std::array<StationBox, 13> host_boxes = {
        g.masthead_title, g.ambient_chrome, g.clock_hero,
        g.odo_hero,       g.today_line,     g.streak_line,
        g.months,         g.grid,           g.spine_divider,
        g.skyline_label,  g.skyline_delta,  g.skyline_plot,
        g.scale_label,
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
}

void test_split_heatmap_columns_align_flush_across_all_three_presets() {
  struct PresetSpec {
    size_t cols;
    int16_t expected_w;
    int16_t min_gap;
  };
  const PresetSpec presets[] = {
      {kMonumentGridCols1Y, 3, 1},
      {kMonumentGridCols6M, 7, 1},
      {kMonumentGridCols3M, 15, 2},
  };
  for (const PresetSpec& p : presets) {
    const MonumentGridCol first = station_grid_col(0, p.cols);
    const MonumentGridCol last = station_grid_col(p.cols - 1, p.cols);
    TEST_ASSERT_EQUAL(kStationGridX, first.x);
    TEST_ASSERT_EQUAL(p.expected_w, first.w);
    TEST_ASSERT_EQUAL(kStationGridRight, last.x + last.w);
    for (size_t c = 1; c < p.cols; c++) {
      const MonumentGridCol prev = station_grid_col(c - 1, p.cols);
      const MonumentGridCol cur = station_grid_col(c, p.cols);
      TEST_ASSERT_GREATER_OR_EQUAL(p.min_gap, cur.x - (prev.x + prev.w));
    }
  }
}

void test_station_provider_rows_and_desk_cat_ledges_have_zero_collisions() {
  // Worst-case row cases:
  // A: Gemini + Antigravity (56 + 4 + 12 = 72), 100% (44), 6d 23h (55), 99.9M (48)
  // B: Claude (56), low-fuel [8%] (24 + pill), [23h old] chip (57 + 7), 999M (43)
  const StationRowWidths worst_normal = {72, 44, false, 55, false, 48};
  const StationRowWidths worst_chips = {72, 24, true, 57, true, 48};

  const StationHeroWidths hero_worst = {115, 84, 87, 118, 44, 48, 40,
                                        22,  20, 24, 50,  70, 97};
  const StationSpineGeometry spine = station_spine_geometry(hero_worst, 8);

  const StationBox ledge0 = station_cat_ledge_envelope(0);  // y = 28
  const StationBox ledge1 = station_cat_ledge_envelope(1);  // y = 151
  const StationBox ledge2 = station_cat_ledge_envelope(2);  // y = 272

  // Ledge 0 clears masthead title ("STATION") and Ambient Chrome ("29.9°C · 99%").
  TEST_ASSERT_FALSE(overlaps(ledge0, spine.masthead_title));
  TEST_ASSERT_FALSE(overlaps(ledge0, spine.ambient_chrome));
  TEST_ASSERT_GREATER_OR_EQUAL(10, ledge0.left - spine.masthead_title.right);
  TEST_ASSERT_GREATER_OR_EQUAL(4, spine.ambient_chrome.left - ledge0.right);

  // Ledge 1 clears heatmap grid, skyline plot, latest ground marker, and "7D" label.
  TEST_ASSERT_FALSE(overlaps(ledge1, spine.grid));
  TEST_ASSERT_FALSE(overlaps(ledge1, spine.skyline_plot));
  TEST_ASSERT_FALSE(overlaps(ledge1, spine.skyline_ground_marker));
  TEST_ASSERT_FALSE(overlaps(ledge1, spine.scale_label));
  TEST_ASSERT_GREATER_OR_EQUAL(8, spine.scale_label.left - ledge1.right);

  // Also verify 7D & 6M ground markers clear Ledge 1.
  for (size_t bars : {(size_t)7, (size_t)6}) {
    const StationSpineGeometry sp = station_spine_geometry(hero_worst, bars);
    TEST_ASSERT_FALSE(overlaps(ledge1, sp.skyline_ground_marker));
  }

  for (size_t count : {(size_t)1, (size_t)2, (size_t)3, (size_t)4}) {
    if (count == 4) TEST_ASSERT_EQUAL(27, station_row_pitch(count));
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
          TEST_ASSERT_LESS_OR_EQUAL(kStationRowsBottom, cols[a].bottom);
          // Every provider row (including 4th row Kimi) clears Ledge 1 and Ledge 2.
          TEST_ASSERT_FALSE(overlaps(cols[a], ledge1));
          TEST_ASSERT_FALSE(overlaps(cols[a], ledge2));
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
  RUN_TEST(test_station_hero_split_spine_and_skyline_have_zero_collisions);
  RUN_TEST(test_split_heatmap_columns_align_flush_across_all_three_presets);
  RUN_TEST(test_station_provider_rows_and_desk_cat_ledges_have_zero_collisions);
  RUN_TEST(test_station_dual_source_footers_clear_battery_and_pinned_chip);
  return UNITY_END();
}
