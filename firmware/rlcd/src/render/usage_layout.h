#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace rcpaper {

// Shared 400x300 1-bit chrome & Allowance layout metrics. Keeping these pure
// lets native tests prove zero collisions across 1..4 rows and worst-case labels.
constexpr int16_t kUsageMarginX = 12;
constexpr int16_t kUsageRightX = 388;
constexpr int16_t kUsageHeaderRuleY = 28;
constexpr int16_t kUsageHeaderBaseline = 19;
constexpr int16_t kUsageBodyTop = 29;
constexpr int16_t kUsageBodyBottom = 272;
constexpr int16_t kUsageBodyHeight = kUsageBodyBottom - kUsageBodyTop;  // 243 px
constexpr int16_t kUsageFooterRuleY = 272;
constexpr int16_t kUsageFooterBaseline = 291;
constexpr int16_t kUsageBatteryY = 280;

constexpr int16_t kUsageChipPadding = 4;  // visual pad (3 px + 1 px trailing tracking)
constexpr int16_t kUsageChipExtraW = 7;   // total horizontal padding added to tracked chip advance
constexpr int16_t kUsageModeGap = 4;
constexpr int16_t kUsageBatteryGap = 4;
constexpr int16_t kUsageFooterTextGap = 6;

constexpr int16_t kUsageLogoX = 12;
constexpr int16_t kUsageLogoSize = 28;
constexpr int16_t kUsageNumRight = 126;
constexpr int16_t kUsagePctX = 128;
constexpr int16_t kUsageRoadX0 = 164;
constexpr int16_t kUsageDrawRoadPx = 132;
constexpr int16_t kUsageRoadX1 = kUsageRoadX0 + kUsageDrawRoadPx;  // 296
constexpr int16_t kUsagePumpX = kUsageRoadX1 + 4;                 // 300
constexpr int16_t kUsagePumpW = 10;
constexpr int16_t kUsageClockW = 9;
constexpr int16_t kUsageClockGap = 4;
constexpr int16_t kUsageCarW = 22;
constexpr int16_t kUsageCarOffsetY = -13;
constexpr int16_t kUsageCarBobMax = 1;
constexpr int16_t kUsagePaceBottomOffset = 11;  // 3 px triangle at road_y + 8

struct UsageRowMetrics {
  int16_t pitch;
  int16_t name_base;
  int16_t road_y;
  int16_t logo_y;
  int16_t num_base;
  int16_t low_box_top;
  int16_t low_box_h;
};

inline UsageRowMetrics usage_row_metrics(size_t count) {
  if (count >= 4) return {60, 22, 43, 16, 49, 11, 42};
  if (count == 3) return {78, 28, 53, 25, 59, 19, 44};
  return {92, 35, 60, 32, 66, 26, 44};
}

inline int16_t usage_row_top(size_t index, size_t count) {
  const UsageRowMetrics m = usage_row_metrics(count);
  const size_t clamped = std::max<size_t>(1, count);
  return kUsageBodyTop +
         (int16_t)((kUsageBodyHeight - m.pitch * (int)clamped) / 2) +
         (int16_t)index * m.pitch;
}

struct UsageBox {
  int16_t left;
  int16_t top;
  int16_t right;
  int16_t bottom;
};

struct UsageTextWidths {
  int16_t num;
  int16_t pct;
  int16_t name_with_badge;
  int16_t right_label;
  bool right_is_chip;
  int16_t countdown;
};

struct UsageRowGeometry {
  UsageBox logo;
  UsageBox number;
  UsageBox low_box;
  UsageBox name;
  UsageBox right_label;
  UsageBox road;
  UsageBox car_bobbing;
  UsageBox pace_tick;
  UsageBox pump;
  UsageBox clock_countdown;
};

inline UsageRowGeometry usage_row_geometry(size_t index, const UsageTextWidths& text,
                                           size_t count) {
  const UsageRowMetrics m = usage_row_metrics(count);
  const int16_t top = usage_row_top(index, count);
  const int16_t name_base = top + m.name_base;
  const int16_t road_y = top + m.road_y;
  const int16_t num_base = top + m.num_base;
  const int16_t right_w =
      text.right_is_chip ? text.right_label + kUsageChipExtraW : text.right_label;
  const int16_t right_top = text.right_is_chip ? name_base - 13 : name_base - 12;
  const int16_t right_bottom = text.right_is_chip ? name_base + 4 : name_base + 5;
  const int16_t clock_left =
      kUsageRightX - text.countdown - kUsageClockGap - kUsageClockW;
  return {
      {kUsageLogoX, (int16_t)(top + m.logo_y), (int16_t)(kUsageLogoX + kUsageLogoSize),
       (int16_t)(top + m.logo_y + kUsageLogoSize)},
      {(int16_t)(kUsageNumRight - text.num), (int16_t)(num_base - 33),
       (int16_t)(kUsagePctX + text.pct), (int16_t)(num_base + 1)},
      {(int16_t)(kUsageNumRight - text.num - 6), (int16_t)(top + m.low_box_top),
       (int16_t)(kUsageNumRight + text.pct + 8),
       (int16_t)(top + m.low_box_top + m.low_box_h)},
      {kUsageRoadX0, (int16_t)(name_base - 17),
       (int16_t)(kUsageRoadX0 + text.name_with_badge), (int16_t)(name_base + 1)},
      {(int16_t)(kUsageRightX - right_w), right_top, kUsageRightX, right_bottom},
      {kUsageRoadX0, road_y, kUsageRoadX1, (int16_t)(road_y + 7)},
      {kUsageRoadX0, (int16_t)(road_y + kUsageCarOffsetY - kUsageCarBobMax),
       kUsageRoadX1, (int16_t)(road_y + 1)},
      {kUsageRoadX0, road_y, kUsageRoadX1, (int16_t)(road_y + kUsagePaceBottomOffset)},
      {kUsagePumpX, (int16_t)(road_y - 6), (int16_t)(kUsagePumpX + kUsagePumpW),
       (int16_t)(road_y + 7)},
      {clock_left, (int16_t)(road_y - 5), kUsageRightX, (int16_t)(road_y + 8)},
  };
}

struct UsageFooterWidths {
  int16_t status_chip;
  int16_t status_text;
  int16_t runtime_chip;
  int16_t battery_text;
};

struct UsageFooterGeometry {
  UsageBox status;
  UsageBox battery;
};

inline UsageFooterGeometry usage_footer_geometry(const UsageFooterWidths& text) {
  const int16_t status_right = kUsageMarginX + text.status_chip +
                               kUsageChipExtraW + kUsageFooterTextGap +
                               text.status_text;
  const int16_t battery_left = kUsageRightX -
                               (text.runtime_chip + kUsageChipExtraW) -
                               kUsageModeGap - text.battery_text -
                               kUsageBatteryGap - 24;
  return {
      {kUsageMarginX, (int16_t)(kUsageFooterBaseline - 13), status_right,
       (int16_t)(kUsageFooterBaseline + 4)},
      {battery_left, (int16_t)(kUsageFooterBaseline - 13), kUsageRightX,
       (int16_t)(kUsageFooterBaseline + 4)},
  };
}

}  // namespace rcpaper
