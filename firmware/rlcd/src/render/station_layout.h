#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "render/monument_layout.h"

namespace rcpaper {

// Pure pixel-box contract for Page 1 ("desk-station": Monument Spine home
// overview uniting Clock/LAST SYNC, Total ODO, 7-row Heatmap Window, and
// 1..4 horizontal Provider Allowance + 7D Token strips) on 400x300 1-bit RLCD.
constexpr int16_t kStationMarginX = 12;
constexpr int16_t kStationRightX = 388;
constexpr int16_t kStationHeaderBaseline = 19;
constexpr int16_t kStationHeaderRuleY = 28;

// Hero band (y = 29..76)
constexpr int16_t kStationHeroBaseline = 69;
constexpr int16_t kStationEcoCaptionBaseline = 41;
constexpr int16_t kStationEcoClockBaseline = 70;
constexpr int16_t kStationHeroSepX = 138;
constexpr int16_t kStationOdoX = 150;
constexpr int16_t kStationTodayBaseline = 47;
constexpr int16_t kStationTodayGap = 5;
constexpr int16_t kStationStreakBaseline = 69;
constexpr int16_t kStationWindowChipGap = 6;
constexpr int16_t kStationHeroDividerY = 76;

// Heatmap spine (y = 77..151) — preserves Monument's exact 369x47 brick matrix
constexpr int16_t kStationMonthBaseline = 89;
constexpr int16_t kStationGridX = kMonumentGridX;          // 19
constexpr int16_t kStationGridY = 93;
constexpr int16_t kStationCellSize = kMonumentCellSize;    // 5
constexpr int16_t kStationCellPitch = kMonumentCellPitch;  // 7
constexpr int16_t kStationGridW = kMonumentGridW;          // 369 -> right = 388
constexpr int16_t kStationGridH = kMonumentGridH;          // 47  -> bottom = 140
constexpr int16_t kStationGridDividerY = 151;
constexpr int16_t kStationScaleLabelBaseline = 155;
constexpr int16_t kStationScaleLabelGap = 6;

// Provider telemetry strips (y = 156..271)
constexpr int16_t kStationRowsTop = 156;
constexpr int16_t kStationRowsBottom = 271;
constexpr int16_t kStationRowsHeight = kStationRowsBottom - kStationRowsTop;  // 115 px

constexpr int16_t kStationLogoX = 12;
constexpr int16_t kStationLogoSize = 15;
constexpr int16_t kStationNameX = 31;
constexpr int16_t kStationAntigravityGap = 4;
constexpr int16_t kStationAntigravitySize = 12;
constexpr int16_t kStationPctRight = 158;
constexpr int16_t kStationBarX = 168;
constexpr int16_t kStationBarPx = 80;
constexpr int16_t kStationBarRight = kStationBarX + kStationBarPx;  // 248
constexpr int16_t kStationBarH = 7;
constexpr int16_t kStationBarOffsetY = -10;
constexpr int16_t kStationPaceBottomOffset = 11;  // 3 px triangle at bar_y + 8
constexpr int16_t kStationCountdownRight = 318;
constexpr int16_t kStationColSepX = 329;
constexpr int16_t kStationTokenRight = 388;
constexpr int16_t kStationDashW = 12;
constexpr int16_t kStationDashH = 2;

// Shared footer (y = 272..300)
constexpr int16_t kStationFooterRuleY = 272;
constexpr int16_t kStationFooterBaseline = 291;
constexpr int16_t kStationChipExtraW = 7;
constexpr int16_t kStationFooterTextGap = 6;
constexpr int16_t kStationModeGap = 4;
constexpr int16_t kStationBatteryGap = 4;

inline int16_t station_row_pitch(size_t count) {
  if (count >= 4) return 28;
  if (count == 3) return 38;
  return 44;
}

inline int16_t station_row_top(size_t index, size_t count) {
  const int16_t pitch = station_row_pitch(count);
  const size_t clamped = std::max<size_t>(1, count);
  return kStationRowsTop +
         (int16_t)((kStationRowsHeight - pitch * (int)clamped) / 2) +
         (int16_t)index * pitch;
}

inline int16_t station_row_baseline(size_t index, size_t count) {
  return station_row_top(index, count) + station_row_pitch(count) / 2 + 5;
}

struct StationBox {
  int16_t left;
  int16_t top;
  int16_t right;
  int16_t bottom;
};

struct StationHeroWidths {
  int16_t clock_hero;        // HeroLg "23:59" (115)
  int16_t eco_caption;       // Chip "LAST SYNC" (84)
  int16_t eco_clock;         // HeroMd "23:59" (87)
  int16_t odo_hero;          // HeroLg "99.9M" (118)
  int16_t today_word;        // Body "today" (44)
  int16_t today_val;         // BodyBold "99.9M" (48)
  int16_t streak_text;       // Body "365d" (40)
  int16_t window_chip_text;  // Chip "6M" (22)
  int16_t scale_label_text;  // Chip "7D" (20)
};

struct StationSpineGeometry {
  StationBox clock_hero;
  StationBox eco_caption;
  StationBox eco_clock;
  StationBox odo_hero;
  StationBox today_line;
  StationBox streak_line;
  StationBox months;
  StationBox grid;
  StationBox scale_label;
};

inline StationSpineGeometry station_spine_geometry(const StationHeroWidths& w) {
  const int16_t today_w = w.today_word + kStationTodayGap + w.today_val;
  const int16_t win_chip_w = w.window_chip_text + kStationChipExtraW;
  const int16_t streak_w = w.streak_text + kStationWindowChipGap + win_chip_w;
  return {
      {kStationMarginX, (int16_t)(kStationHeroBaseline - 33),
       (int16_t)(kStationMarginX + w.clock_hero),
       (int16_t)(kStationHeroBaseline + 1)},
      {kStationMarginX, (int16_t)(kStationEcoCaptionBaseline - 10),
       (int16_t)(kStationMarginX + w.eco_caption),
       (int16_t)(kStationEcoCaptionBaseline + 1)},
      {kStationMarginX, (int16_t)(kStationEcoClockBaseline - 24),
       (int16_t)(kStationMarginX + w.eco_clock),
       (int16_t)(kStationEcoClockBaseline + 1)},
      {kStationOdoX, (int16_t)(kStationHeroBaseline - 33),
       (int16_t)(kStationOdoX + w.odo_hero),
       (int16_t)(kStationHeroBaseline + 1)},
      {(int16_t)(kStationRightX - today_w), (int16_t)(kStationTodayBaseline - 12),
       kStationRightX, (int16_t)(kStationTodayBaseline + 4)},
      {(int16_t)(kStationRightX - streak_w), (int16_t)(kStationStreakBaseline - 13),
       kStationRightX, (int16_t)(kStationStreakBaseline + 4)},
      {kStationGridX, (int16_t)(kStationMonthBaseline - 10), kStationRightX,
       (int16_t)(kStationMonthBaseline + 1)},
      {kStationGridX, kStationGridY, (int16_t)(kStationGridX + kStationGridW),
       (int16_t)(kStationGridY + kStationGridH)},
      {(int16_t)(kStationRightX - w.scale_label_text),
       (int16_t)(kStationScaleLabelBaseline - 10), kStationRightX,
       (int16_t)(kStationScaleLabelBaseline + 1)},
  };
}

struct StationRowWidths {
  int16_t name_with_mark;  // BodyBold "Gemini" (56) + 4 + 12 = 72
  int16_t pct_text;        // BodyBold "100%" (44) or "8%" (24)
  bool low_pill;           // true when <=10% live
  int16_t countdown_text;  // Body "6d 23h" (55) or Chip "23h old" (57)
  bool countdown_is_chip;
  int16_t token_text;      // BodyBold "99.9M" (48) or kStationDashW (12)
};

struct StationRowGeometry {
  StationBox identity;
  StationBox pct;
  StationBox bar;
  StationBox countdown;
  StationBox token;
};

inline StationRowGeometry station_row_geometry(size_t index, size_t count,
                                               const StationRowWidths& w) {
  const int16_t base = station_row_baseline(index, count);
  const int16_t pct_left =
      w.low_pill ? (int16_t)(kStationPctRight - w.pct_text - 4)
                 : (int16_t)(kStationPctRight - w.pct_text);
  const int16_t pct_right =
      w.low_pill ? (int16_t)(kStationPctRight + 4) : kStationPctRight;
  const int16_t cd_w =
      w.countdown_is_chip ? (int16_t)(w.countdown_text + kStationChipExtraW)
                          : w.countdown_text;
  const int16_t bar_y = base + kStationBarOffsetY;
  return {
      {kStationLogoX, (int16_t)(base - 13),
       (int16_t)(kStationNameX + w.name_with_mark), (int16_t)(base + 2)},
      {pct_left, (int16_t)(base - 13), pct_right, (int16_t)(base + 4)},
      {kStationBarX, bar_y, kStationBarRight,
       (int16_t)(bar_y + kStationPaceBottomOffset)},
      {(int16_t)(kStationCountdownRight - cd_w), (int16_t)(base - 13),
       kStationCountdownRight, (int16_t)(base + 4)},
      {(int16_t)(kStationTokenRight - w.token_text), (int16_t)(base - 13),
       kStationTokenRight, (int16_t)(base + 1)},
  };
}

struct StationFooterWidths {
  int16_t status_chip;  // 0 if no chip
  int16_t status_text;
  int16_t runtime_chip;
  int16_t battery_text;
};

struct StationFooterGeometry {
  StationBox status;
  StationBox battery;
};

inline StationFooterGeometry station_footer_geometry(
    const StationFooterWidths& w) {
  const int16_t status_right =
      w.status_chip > 0
          ? (int16_t)(kStationMarginX + w.status_chip + kStationChipExtraW +
                      kStationFooterTextGap + w.status_text)
          : (int16_t)(kStationMarginX + w.status_text);
  const int16_t battery_left =
      kStationRightX - (w.runtime_chip + kStationChipExtraW) - kStationModeGap -
      w.battery_text - kStationBatteryGap - 24;
  return {
      {kStationMarginX, (int16_t)(kStationFooterBaseline - 13), status_right,
       (int16_t)(kStationFooterBaseline + 4)},
      {battery_left, (int16_t)(kStationFooterBaseline - 13), kStationRightX,
       (int16_t)(kStationFooterBaseline + 4)},
  };
}

}  // namespace rcpaper
