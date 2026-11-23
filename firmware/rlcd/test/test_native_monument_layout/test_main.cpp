#include <unity.h>

#include <array>
#include <cstddef>

#include "render/monument_layout.h"

using namespace rcpaper;

namespace {

bool overlaps(const MonumentBox& a, const MonumentBox& b) {
  return a.left < b.right && b.left < a.right &&
         a.top < b.bottom && b.top < a.bottom;
}

}  // namespace

void test_monument_layout_has_zero_collisions_and_clean_margins() {
  // Worst-case Barlow widths measured with tools/bake_rlcd_fonts.py:
  // clock_caption="LAST SYNC" (85), clock_hhmm="23:59" (115),
  // odo_caption="TOTAL ODO" (90), active_badge="365 DAYS" (75 in Body18),
  // total_odo="999B" (113), summary_left="today 1.21B · 365d streak" (200),
  // summary_right="peak 1.62B" (95), milestone_label="NEXT 100B" (90),
  // milestone_pct="99%" (34), legend_less="LESS" (38), legend_more="MORE" (42),
  // climate_left="DESK CLIMATE  29.9°C · 99% RH" (230),
  // climate_verdict="COMFORTABLE" (108 chip).
  const MonumentHeroWidths hero = {85, 115, 90, 75, 113, 200, 95};
  const MonumentLowerWidths lower = {90, 34, 38, 42, 230, 108};
  const MonumentLayoutGeometry geom = monument_layout_geometry(hero, lower);

  // Horizontal separation & margin checks.
  TEST_ASSERT_GREATER_OR_EQUAL(16, kMonumentHeroSepX - geom.clock_hero.right);
  TEST_ASSERT_GREATER_OR_EQUAL(8, geom.odo_hero.left - kMonumentHeroSepX);
  TEST_ASSERT_GREATER_OR_EQUAL(12, geom.active_badge.left - geom.odo_caption.right);
  TEST_ASSERT_GREATER_OR_EQUAL(20, geom.summary_right.left - geom.summary_left.right);
  TEST_ASSERT_EQUAL(388, geom.grid.right);
  TEST_ASSERT_GREATER_OR_EQUAL(24, geom.milestone_bar.right - geom.milestone_bar.left);
  TEST_ASSERT_GREATER_OR_EQUAL(8, geom.milestone_pct.left - geom.milestone_bar.right);
  TEST_ASSERT_GREATER_OR_EQUAL(8, geom.legend.left - geom.milestone_pct.right);
  TEST_ASSERT_GREATER_OR_EQUAL(8, geom.climate_verdict.left - geom.climate_left.right);

  // Vertical separation checks.
  TEST_ASSERT_GREATER_OR_EQUAL(4, geom.clock_hero.top - geom.clock_caption.bottom);
  TEST_ASSERT_GREATER_OR_EQUAL(4, geom.odo_hero.top - geom.odo_caption.bottom);
  TEST_ASSERT_GREATER_OR_EQUAL(4, geom.summary_left.top - geom.clock_hero.bottom);
  TEST_ASSERT_GREATER_OR_EQUAL(4, kMonumentHeroDividerY - geom.summary_left.bottom);
  TEST_ASSERT_GREATER_OR_EQUAL(4, geom.months.top - kMonumentHeroDividerY);
  TEST_ASSERT_GREATER_OR_EQUAL(4, geom.grid.top - geom.months.bottom);
  TEST_ASSERT_GREATER_OR_EQUAL(4, kMonumentGridDividerY - geom.grid.bottom);
  TEST_ASSERT_GREATER_OR_EQUAL(4, geom.milestone_bar.top - kMonumentGridDividerY);
  TEST_ASSERT_GREATER_OR_EQUAL(8, geom.climate_left.top - geom.milestone_label.bottom);
  TEST_ASSERT_GREATER_OR_EQUAL(8, kMonumentFooterRuleY - geom.climate_left.bottom);

  const std::array<MonumentBox, 15> all = {
      geom.clock_caption,  geom.clock_hero,      geom.odo_caption,
      geom.active_badge,   geom.odo_hero,        geom.summary_left,
      geom.summary_right,  geom.months,          geom.grid,
      geom.milestone_label, geom.milestone_bar,  geom.milestone_pct,
      geom.legend,         geom.climate_left,    geom.climate_verdict,
  };
  for (size_t i = 0; i < all.size(); i++) {
    TEST_ASSERT_GREATER_OR_EQUAL(0, all[i].left);
    TEST_ASSERT_LESS_OR_EQUAL(400, all[i].right);
    TEST_ASSERT_GREATER_OR_EQUAL(29, all[i].top);
    TEST_ASSERT_LESS_OR_EQUAL(kMonumentFooterRuleY, all[i].bottom);
    for (size_t j = i + 1; j < all.size(); j++) {
      TEST_ASSERT_FALSE(overlaps(all[i], all[j]));
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_monument_layout_has_zero_collisions_and_clean_margins);
  return UNITY_END();
}
