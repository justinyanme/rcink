#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "power/runtime_policy.h"
#include "render/monument_view.h"
#include "render/station_layout.h"
#include "render/token_view.h"
#include "render/usage_view.h"

namespace rcpaper {

constexpr const char* kStationPageId = "desk-station";

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
  std::string header_left;
  bool eco_last_sync = false;
  std::string clock_hhmm;
  uint64_t total_odo_value = 0;
  std::string total_odo;
  std::string today_total;
  std::string streak_label;
  std::string window_label = "1Y";
  uint8_t cols = (uint8_t)kMonumentMaxCols;
  std::array<uint8_t, kMonumentMaxCols * kMonumentRows> cells{};
  std::array<MonumentMonthTick, 12> months{};
  size_t month_count = 0;
  std::array<StationRowView, kUsageMaxProviders> rows{};
  size_t row_count = 0;
  std::string footer_chip;
  std::string footer_text;
};

inline bool operator==(const StationView& a, const StationView& b) {
  if (a.header_left != b.header_left || a.eco_last_sync != b.eco_last_sync ||
      a.clock_hhmm != b.clock_hhmm || a.total_odo_value != b.total_odo_value ||
      a.total_odo != b.total_odo || a.today_total != b.today_total ||
      a.streak_label != b.streak_label || a.window_label != b.window_label ||
      a.cols != b.cols || a.cells != b.cells ||
      a.month_count != b.month_count || a.row_count != b.row_count ||
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

StationView derive_station_view(const UsageSnapshot& usage,
                                const UsageSync& usage_sync,
                                const TokenSnapshot& token,
                                const MonumentSnapshot& monument,
                                const TokenSync& token_sync, uint64_t now,
                                RuntimeMode runtime_mode,
                                HeatmapWindow window = HeatmapWindow::Year);
std::string encode_station_view(const StationView& view);
bool decode_station_view(const std::string& json, StationView& out);
PageRecord station_page(const UsageSnapshot& usage, const UsageSync& usage_sync,
                        const TokenSnapshot& token,
                        const MonumentSnapshot& monument,
                        const TokenSync& token_sync, uint64_t now,
                        RuntimeMode runtime_mode,
                        HeatmapWindow window = HeatmapWindow::Year);
bool update_station_page(DeviceState& device, const UsageSnapshot& usage,
                         const UsageSync& usage_sync,
                         const TokenSnapshot& token,
                         const MonumentSnapshot& monument,
                         const TokenSync& token_sync, uint64_t now,
                         RuntimeMode runtime_mode,
                         HeatmapWindow window = HeatmapWindow::Year);

}  // namespace rcpaper
