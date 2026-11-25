#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "render/token_view.h"

namespace rcpaper {

// Pixel boxes for the fixed FreeSans glyph runs and chart. Bottom/right are
// exclusive. These offsets are shared with the hand-drawn renderer so native
// tests check the geometry that actually reaches the framebuffer.
constexpr int16_t kTokenMarginX = 12;
constexpr int16_t kTokenHeaderRuleY = 28;
constexpr int16_t kTokenHeaderBaseline = 19;
constexpr int16_t kTokenBodyTop = 29;
constexpr int16_t kTokenBodyBottom = 272;
constexpr int16_t kTokenBodyHeight = kTokenBodyBottom - kTokenBodyTop;  // 243 px
constexpr int16_t kTokenFooterRuleY = 272;
constexpr int16_t kTokenFooterBaseline = 291;
constexpr int16_t kTokenBatteryY = 280;

constexpr int16_t kTokenTextX = 50;
constexpr int16_t kTokenNameLabelGap = 10;
constexpr int16_t kTokenChartX = 210;
constexpr int16_t kTokenChartRight = 388;  // 210 + 29*6 + 4 = 388 exact right alignment
constexpr int16_t kTokenOdoGap = 6;
constexpr int16_t kTokenDotGap = 5;
constexpr int16_t kTokenChipPadding = 4;  // visual pad (3 px + 1 px trailing tracking)
constexpr int16_t kTokenChipExtraW = 7;
constexpr int16_t kTokenModeGap = 4;
constexpr int16_t kTokenBatteryGap = 4;
constexpr int16_t kTokenFooterTextGap = 6;

struct TokenRowMetrics {
  int16_t pitch;
  int16_t total_pt;
  int16_t hdr_base;
  int16_t total_base;
  int16_t chart_h;
  int16_t today_base;
  int16_t logo_y;
  bool compact_name;
};

inline TokenRowMetrics token_row_metrics(size_t count) {
  if (count >= 4) return {60, 12, 16, 39, 18, 54, 16, true};
  if (count == 3) return {80, 18, 19, 54, 28, 71, 26, false};
  return {114, 24, 25, 74, 42, 98, 43, false};
}

struct TokenBox {
  int16_t left;
  int16_t top;
  int16_t right;
  int16_t bottom;
};

struct TokenTextWidths {
  int16_t provider;
  int16_t total;
  int16_t today;     // includes symmetric spacing around the drawn dot
  int16_t odometer;  // ODO, spacing, and value
};

struct TokenRowGeometry {
  TokenBox provider;
  TokenBox seven_days;
  TokenBox total;
  TokenBox chart;
  TokenBox today;
  TokenBox odometer;
};

struct TokenFooterWidths {
  int16_t status_chip;
  int16_t status_text;
  int16_t runtime_chip;
  int16_t battery_text;
};

struct TokenFooterGeometry {
  TokenBox status;
  TokenBox battery;
};

inline TokenFooterGeometry token_footer_geometry(const TokenFooterWidths& text) {
  const int16_t status_right = kTokenMarginX + text.status_chip +
                               kTokenChipExtraW + kTokenFooterTextGap +
                               text.status_text;
  const int16_t battery_left = kTokenChartRight -
                               (text.runtime_chip + kTokenChipExtraW) -
                               kTokenModeGap - text.battery_text -
                               kTokenBatteryGap - 24;
  return {
      {kTokenMarginX, (int16_t)(kTokenFooterBaseline - 13), status_right,
       (int16_t)(kTokenFooterBaseline + 4)},
      {battery_left, (int16_t)(kTokenFooterBaseline - 13), kTokenChartRight,
       (int16_t)(kTokenFooterBaseline + 4)},
  };
}

struct TokenHeadlineGeometry {
  TokenBox headline;
  TokenBox since;
};

inline TokenHeadlineGeometry token_headline_geometry(int16_t headline_width,
                                                     int16_t since_width,
                                                     bool has_pick = true) {
  const int16_t left = has_pick ? 26 : kTokenMarginX;
  return {
      {left, 0, (int16_t)(left + headline_width), kTokenHeaderRuleY},
      {(int16_t)(kTokenChartRight - since_width), 0, kTokenChartRight,
       kTokenHeaderRuleY},
  };
}

inline int16_t token_row_top(size_t index, size_t count) {
  const TokenRowMetrics m = token_row_metrics(count);
  const size_t clamped = std::max<size_t>(1, count);
  return kTokenBodyTop +
         (int16_t)((kTokenBodyHeight - m.pitch * (int)clamped) / 2) +
         (int16_t)index * m.pitch;
}

inline TokenRowGeometry token_row_geometry(size_t index, const TokenTextWidths& text,
                                           size_t count = kTokenMaxProviders) {
  const TokenRowMetrics m = token_row_metrics(count);
  const int16_t top = token_row_top(index, count);
  const int16_t hdr_base = top + m.hdr_base;
  const int16_t total_base = top + m.total_base;
  const int16_t today_base = top + m.today_base;
  const int16_t seven_x = kTokenTextX + text.provider + kTokenNameLabelGap;
  const int16_t provider_asc = m.compact_name ? 13 : 17;
  const int16_t total_asc =
      m.total_pt >= 24 ? 33 : (m.total_pt >= 18 ? 24 : 17);
  const int16_t total_desc = 1;
  return {
      {kTokenTextX, (int16_t)(hdr_base - provider_asc),
       (int16_t)(kTokenTextX + text.provider), (int16_t)(hdr_base + 1)},
      {seven_x, (int16_t)(hdr_base - 11), (int16_t)(seven_x + 51),
       (int16_t)(hdr_base + 4)},
      {kTokenTextX, (int16_t)(total_base - total_asc),
       (int16_t)(kTokenTextX + text.total), (int16_t)(total_base + total_desc)},
      {kTokenChartX, (int16_t)(total_base - m.chart_h),
       kTokenChartRight, (int16_t)(total_base + 1)},
      {kTokenTextX, (int16_t)(today_base - 11), (int16_t)(kTokenTextX + text.today),
       (int16_t)(today_base + 4)},
      {(int16_t)(kTokenChartRight - text.odometer), (int16_t)(hdr_base - 12),
       kTokenChartRight, (int16_t)(hdr_base + 1)},
  };
}

}  // namespace rcpaper
