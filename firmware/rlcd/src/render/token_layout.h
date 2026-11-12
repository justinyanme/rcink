#pragma once

#include <cstddef>
#include <cstdint>

#include "render/token_view.h"

namespace rcpaper {

// Pixel boxes for the fixed FreeSans glyph runs and chart. Bottom/right are
// exclusive. These offsets are shared with the hand-drawn renderer so native
// tests check the geometry that actually reaches the framebuffer.
constexpr int16_t kTokenRowTop = 26;
constexpr int16_t kTokenRowPitch = 82;
constexpr int16_t kTokenTextX = 48;
constexpr int16_t kTokenSevenDaysX = 150;
constexpr int16_t kTokenChartX = 202;
constexpr int16_t kTokenChartRight = 388;
constexpr int16_t kTokenTopBaseline = 14;
constexpr int16_t kTokenTotalBaseline = 50;
constexpr int16_t kTokenChartBottom = 53;
constexpr int16_t kTokenTodayBaseline = 71;
constexpr int16_t kTokenChipPadding = 3;
constexpr int16_t kTokenModeGap = 4;
constexpr int16_t kTokenBatteryGap = 4;
constexpr int16_t kTokenFooterTextGap = 6;

struct TokenBox {
  int16_t left;
  int16_t top;
  int16_t right;
  int16_t bottom;
};

struct TokenTextWidths {
  int16_t provider;
  int16_t total;
  int16_t today;     // includes the manually drawn middle dot
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
  const int16_t status_right = 12 + text.status_chip + 2 * kTokenChipPadding +
                               kTokenFooterTextGap + text.status_text;
  const int16_t battery_left = kTokenChartRight -
      (text.runtime_chip + 2 * kTokenChipPadding) - kTokenModeGap -
      text.battery_text - kTokenBatteryGap - 24;
  return {
      {12, 279, status_right, 299},
      {battery_left, 279, kTokenChartRight, 299},
  };
}

struct TokenHeadlineGeometry {
  TokenBox headline;
  TokenBox since;
};

inline TokenHeadlineGeometry token_headline_geometry(int16_t headline_width,
                                                     int16_t since_width) {
  return {
      {28, 0, (int16_t)(28 + headline_width), 24},
      {(int16_t)(kTokenChartRight - since_width), 0, kTokenChartRight, 24},
  };
}

inline TokenRowGeometry token_row_geometry(size_t index, const TokenTextWidths& text) {
  const int16_t top = kTokenRowTop + (int16_t)index * kTokenRowPitch;
  return {
      {kTokenTextX, (int16_t)(top + 2), (int16_t)(kTokenTextX + text.provider),
       (int16_t)(top + 15)},
      {kTokenSevenDaysX, (int16_t)(top + 2), (int16_t)(kTokenSevenDaysX + 53),
       (int16_t)(top + 15)},
      {kTokenTextX, (int16_t)(top + 18), (int16_t)(kTokenTextX + text.total),
       (int16_t)(top + 51)},
      {kTokenChartX, (int16_t)(top + kTokenChartBottom - kTokenChartPx),
       kTokenChartRight, (int16_t)(top + kTokenChartBottom + 1)},
      {kTokenTextX, (int16_t)(top + 58), (int16_t)(kTokenTextX + text.today),
       (int16_t)(top + 77)},
      {(int16_t)(kTokenChartRight - text.odometer), (int16_t)(top + 2),
       kTokenChartRight, (int16_t)(top + 15)},
  };
}

}  // namespace rcpaper
