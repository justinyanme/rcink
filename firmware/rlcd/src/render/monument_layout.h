#pragma once

#include <cstdint>

namespace rcpaper {

// Pure pixel boxes for Page 3 ("year-monument": Clock + Total ODO + 53x7
// Annual Heatmap + Milestone & Desk Micro-Climate) on the 400x300 1-bit RLCD.
constexpr int16_t kMonumentMarginX = 12;
constexpr int16_t kMonumentRightX = 388;
constexpr int16_t kMonumentHeaderBaseline = 19;
constexpr int16_t kMonumentHeaderRuleY = 28;

constexpr int16_t kMonumentHeroSepX = 196;
constexpr int16_t kMonumentOdoX = 208;
constexpr int16_t kMonumentHeroCaptionBaseline = 42;
constexpr int16_t kMonumentHeroBaseline = 88;
constexpr int16_t kMonumentSummaryBaseline = 108;
constexpr int16_t kMonumentHeroDividerY = 116;

constexpr int16_t kMonumentMonthBaseline = 133;
constexpr int16_t kMonumentGridX = 19;
constexpr int16_t kMonumentGridY = 140;
constexpr int16_t kMonumentCellSize = 5;
constexpr int16_t kMonumentCellPitch = 7;
constexpr int16_t kMonumentGridW = 53 * kMonumentCellPitch - 2;  // 369 px -> right = 388
constexpr int16_t kMonumentGridH = 7 * kMonumentCellPitch - 2;   // 47 px -> bottom = 187
constexpr int16_t kMonumentGridDividerY = 194;

constexpr int16_t kMonumentMilestoneBaseline = 216;
constexpr int16_t kMonumentMilestoneBarY = 205;
constexpr int16_t kMonumentMilestoneBarH = 10;
constexpr int16_t kMonumentClimateBaseline = 252;

constexpr int16_t kMonumentFooterRuleY = 272;
constexpr int16_t kMonumentFooterBaseline = 291;

struct MonumentBox {
  int16_t left;
  int16_t top;
  int16_t right;
  int16_t bottom;
};

struct MonumentHeroWidths {
  int16_t clock_caption;
  int16_t clock_hhmm;
  int16_t odo_caption;
  int16_t active_badge;
  int16_t total_odo;
  int16_t summary_left;
  int16_t summary_right;
};

struct MonumentLowerWidths {
  int16_t milestone_label;
  int16_t milestone_pct;
  int16_t legend_less;
  int16_t legend_more;
  int16_t climate_left;
  int16_t climate_verdict;
};

struct MonumentLayoutGeometry {
  MonumentBox clock_caption;
  MonumentBox clock_hero;
  MonumentBox odo_caption;
  MonumentBox active_badge;
  MonumentBox odo_hero;
  MonumentBox summary_left;
  MonumentBox summary_right;
  MonumentBox months;
  MonumentBox grid;
  MonumentBox milestone_label;
  MonumentBox milestone_bar;
  MonumentBox milestone_pct;
  MonumentBox legend;
  MonumentBox climate_left;
  MonumentBox climate_verdict;
};

inline MonumentLayoutGeometry monument_layout_geometry(
    const MonumentHeroWidths& hero, const MonumentLowerWidths& lower) {
  const int16_t legend_cells_w = 5 * 5 + 4 * 3;  // 37 px
  const int16_t legend_w =
      lower.legend_less + 4 + legend_cells_w + 4 + lower.legend_more;
  const int16_t legend_left = kMonumentRightX - legend_w;
  const int16_t pct_right = legend_left - 12;
  const int16_t pct_left = pct_right - lower.milestone_pct;
  const int16_t bar_left = kMonumentMarginX + lower.milestone_label + 10;
  const int16_t bar_right = pct_left - 8;

  return {
      {kMonumentMarginX, (int16_t)(kMonumentHeroCaptionBaseline - 13),
       (int16_t)(kMonumentMarginX + hero.clock_caption),
       (int16_t)(kMonumentHeroCaptionBaseline + 4)},
      {kMonumentMarginX, (int16_t)(kMonumentHeroBaseline - 37),
       (int16_t)(kMonumentMarginX + hero.clock_hhmm),
       (int16_t)(kMonumentHeroBaseline + 1)},
      {kMonumentOdoX, (int16_t)(kMonumentHeroCaptionBaseline - 13),
       (int16_t)(kMonumentOdoX + hero.odo_caption),
       (int16_t)(kMonumentHeroCaptionBaseline + 4)},
      {(int16_t)(kMonumentRightX - hero.active_badge),
       (int16_t)(kMonumentHeroCaptionBaseline - 12), kMonumentRightX,
       (int16_t)(kMonumentHeroCaptionBaseline + 4)},
      {kMonumentOdoX, (int16_t)(kMonumentHeroBaseline - 37),
       (int16_t)(kMonumentOdoX + hero.total_odo),
       (int16_t)(kMonumentHeroBaseline + 1)},
      {kMonumentMarginX, (int16_t)(kMonumentSummaryBaseline - 13),
       (int16_t)(kMonumentMarginX + hero.summary_left),
       (int16_t)(kMonumentSummaryBaseline + 4)},
      {(int16_t)(kMonumentRightX - hero.summary_right),
       (int16_t)(kMonumentSummaryBaseline - 13), kMonumentRightX,
       (int16_t)(kMonumentSummaryBaseline + 4)},
      {kMonumentGridX, (int16_t)(kMonumentMonthBaseline - 12), kMonumentRightX,
       (int16_t)(kMonumentMonthBaseline + 2)},
      {kMonumentGridX, kMonumentGridY,
       (int16_t)(kMonumentGridX + kMonumentGridW),
       (int16_t)(kMonumentGridY + kMonumentGridH)},
      {kMonumentMarginX, (int16_t)(kMonumentMilestoneBaseline - 14),
       (int16_t)(kMonumentMarginX + lower.milestone_label),
       (int16_t)(kMonumentMilestoneBaseline + 5)},
      {bar_left, kMonumentMilestoneBarY, bar_right,
       (int16_t)(kMonumentMilestoneBarY + kMonumentMilestoneBarH)},
      {pct_left, (int16_t)(kMonumentMilestoneBaseline - 13), pct_right,
       (int16_t)(kMonumentMilestoneBaseline + 4)},
      {legend_left, (int16_t)(kMonumentMilestoneBaseline - 12),
       kMonumentRightX, (int16_t)(kMonumentMilestoneBaseline + 4)},
      {kMonumentMarginX, (int16_t)(kMonumentClimateBaseline - 14),
       (int16_t)(kMonumentMarginX + lower.climate_left),
       (int16_t)(kMonumentClimateBaseline + 5)},
      {(int16_t)(kMonumentRightX - lower.climate_verdict),
       (int16_t)(kMonumentClimateBaseline - 14), kMonumentRightX,
       (int16_t)(kMonumentClimateBaseline + 5)},
  };
}

}  // namespace rcpaper
