#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "power/runtime_policy.h"
#include "render/monument_view.h"
#include "render/station_layout.h"
#include "render/token_view.h"
#include "render/usage_view.h"

namespace rcpaper {

constexpr const char* kStationPageId = "desk-station";
constexpr size_t kSkylineMaxBars = 8;

enum class SkylineScale : uint8_t {
  SevenDays = 0,   // 7D daily bars (coupled with 3M heatmap)
  EightWeeks = 1,  // 8W rolling 7-day bars (coupled with 6M heatmap — Station default)
  SixMonths = 2,   // 6M rolling 30-day bars (coupled with 1Y heatmap)
};

inline SkylineScale skyline_scale_for_window(HeatmapWindow window) {
  switch (window) {
    case HeatmapWindow::ThreeMonths: return SkylineScale::SevenDays;
    case HeatmapWindow::Year:        return SkylineScale::SixMonths;
    case HeatmapWindow::SixMonths:
    default:                         return SkylineScale::EightWeeks;
  }
}

inline const char* skyline_scale_label(SkylineScale scale) {
  switch (scale) {
    case SkylineScale::SevenDays: return "7D";
    case SkylineScale::SixMonths: return "6M";
    case SkylineScale::EightWeeks:
    default:                      return "8W";
  }
}

enum class SkyPhase : uint8_t {
  Dawn = 0,   // 05:30–07:30
  Day = 1,    // 07:30–17:00
  Dusk = 2,   // 17:00–19:30
  Night = 3,  // 19:30–05:30
};

struct SkyVisualState {
  SkyPhase phase = SkyPhase::Day;
  bool is_moon = false;
  int16_t body_x = kStationSkylineX + 50;
  int16_t body_y = kStationSkylineTop + 1;
  int16_t cloud1_x = 18;
  int16_t cloud2_x = 72;
  uint8_t twinkle_phase = 0;
  bool beacon_on = true;
};

inline bool operator==(const SkyVisualState& a, const SkyVisualState& b) {
  return a.phase == b.phase && a.is_moon == b.is_moon &&
         a.body_x == b.body_x && a.body_y == b.body_y &&
         a.cloud1_x == b.cloud1_x && a.cloud2_x == b.cloud2_x &&
         a.twinkle_phase == b.twinkle_phase && a.beacon_on == b.beacon_on;
}

SkyPhase sky_phase_for_minutes(uint16_t minutes_of_day);
SkyVisualState derive_sky_visual(uint16_t minutes_of_day, bool animating,
                                 uint32_t tick);

struct TokenSkylineView {
  SkylineScale scale = SkylineScale::EightWeeks;
  std::string label = "8W";
  std::array<uint8_t, kSkylineMaxBars> bar_heights{};
  std::array<uint8_t, kSkylineMaxBars> above_baseline{};
  uint8_t bar_count = 8;
  uint8_t baseline_px = 0;
  int16_t delta_pct = 0;
  bool has_delta = false;
  bool delta_positive = true;
  std::string delta_label = "--";
  bool has_data = false;
  uint64_t current_sum = 0;
  uint64_t prior_sum = 0;
};

inline bool operator==(const TokenSkylineView& a, const TokenSkylineView& b) {
  if (a.scale != b.scale || a.label != b.label || a.bar_count != b.bar_count ||
      a.baseline_px != b.baseline_px || a.delta_pct != b.delta_pct ||
      a.has_delta != b.has_delta || a.delta_positive != b.delta_positive ||
      a.delta_label != b.delta_label || a.has_data != b.has_data) {
    return false;
  }
  for (size_t i = 0; i < a.bar_count && i < kSkylineMaxBars; i++) {
    if (a.bar_heights[i] != b.bar_heights[i] ||
        a.above_baseline[i] != b.above_baseline[i]) {
      return false;
    }
  }
  return true;
}

TokenSkylineView derive_token_skyline(const std::vector<uint64_t>& shifted_days,
                                      bool has_data, SkylineScale scale);

struct StationRowView {
  std::string id;
  UsageRowState state = UsageRowState::Unavailable;
  uint8_t left = 0;
  bool low_fuel = false;
  int16_t car_px = -1;
  int16_t pace_px = -1;
  std::string countdown = "--";
  std::string chip;
  bool has_token = false;
  std::string seven_day;
};

inline bool operator==(const StationRowView& a, const StationRowView& b) {
  return a.id == b.id && a.state == b.state && a.left == b.left &&
         a.low_fuel == b.low_fuel && a.car_px == b.car_px &&
         a.pace_px == b.pace_px && a.countdown == b.countdown &&
         a.chip == b.chip && a.has_token == b.has_token &&
         a.seven_day == b.seven_day;
}

struct StationView {
  std::string header_left = "STATION";
  bool eco_last_sync = false;
  std::string clock_hhmm;
  uint16_t minutes_of_day = 750;
  uint64_t total_odo_value = 0;
  std::string total_odo;
  uint64_t today_tokens = 0;
  std::string today_total;
  std::string streak_label;
  std::string window_label = "6M";
  uint8_t cols = (uint8_t)kMonumentGridCols6M;
  std::array<uint8_t, kMonumentMaxCols * kMonumentRows> cells{};
  std::array<MonumentMonthTick, 12> months{};
  size_t month_count = 0;
  TokenSkylineView skyline;
  std::array<StationRowView, kUsageMaxProviders> rows{};
  size_t row_count = 0;
  std::string footer_chip;
  std::string footer_text;
};

inline bool operator==(const StationView& a, const StationView& b) {
  if (a.header_left != b.header_left || a.eco_last_sync != b.eco_last_sync ||
      a.clock_hhmm != b.clock_hhmm || a.minutes_of_day != b.minutes_of_day ||
      a.total_odo_value != b.total_odo_value ||
      a.total_odo != b.total_odo || a.today_tokens != b.today_tokens ||
      a.today_total != b.today_total ||
      a.streak_label != b.streak_label || a.window_label != b.window_label ||
      a.cols != b.cols || a.cells != b.cells ||
      a.month_count != b.month_count || !(a.skyline == b.skyline) ||
      a.row_count != b.row_count ||
      a.footer_chip != b.footer_chip || a.footer_text != b.footer_text) {
    return false;
  }
  if (!std::equal(a.months.begin(), a.months.begin() + a.month_count,
                  b.months.begin())) {
    return false;
  }
  for (size_t i = 0; i < a.row_count; i++) {
    if (!(a.rows[i] == b.rows[i])) return false;
  }
  return true;
}

StationView derive_station_view(
    const UsageSnapshot& usage, const UsageSync& usage_sync,
    const TokenSnapshot& token, const MonumentSnapshot& monument,
    const TokenSync& token_sync, uint64_t now, RuntimeMode runtime_mode,
    HeatmapWindow window = HeatmapWindow::SixMonths);
std::string encode_station_view(const StationView& view);
bool decode_station_view(const std::string& json, StationView& out);
PageRecord station_page(
    const UsageSnapshot& usage, const UsageSync& usage_sync,
    const TokenSnapshot& token, const MonumentSnapshot& monument,
    const TokenSync& token_sync, uint64_t now, RuntimeMode runtime_mode,
    HeatmapWindow window = HeatmapWindow::SixMonths);
bool update_station_page(
    DeviceState& device, const UsageSnapshot& usage,
    const UsageSync& usage_sync, const TokenSnapshot& token,
    const MonumentSnapshot& monument, const TokenSync& token_sync,
    uint64_t now, RuntimeMode runtime_mode,
    HeatmapWindow window = HeatmapWindow::SixMonths);

}  // namespace rcpaper
