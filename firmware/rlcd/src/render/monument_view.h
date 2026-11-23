#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "power/runtime_policy.h"
#include "render/monument.h"
#include "render/token.h"

namespace rcpaper {

constexpr size_t kMonumentCols = 53;
constexpr size_t kMonumentRows = 7;
constexpr uint8_t kMonumentCellOutside = 255;

struct MonumentMonthTick {
  uint8_t col = 0;
  char label[4] = {};
};

inline bool operator==(const MonumentMonthTick& a, const MonumentMonthTick& b) {
  return a.col == b.col && std::string(a.label) == std::string(b.label);
}

struct MonumentView {
  std::string header_left;
  std::string clock_caption;
  std::string clock_hhmm;
  uint64_t total_odo_value = 0;
  std::string total_odo;
  std::string active_badge;
  std::string summary_left;
  std::string summary_right;
  std::array<uint8_t, kMonumentCols * kMonumentRows> cells{};
  std::array<MonumentMonthTick, 12> months{};
  size_t month_count = 0;
  std::string milestone_label;
  uint8_t milestone_pct = 0;
  std::string footer_chip;
  std::string footer_text;
};

inline bool operator==(const MonumentView& a, const MonumentView& b) {
  return a.header_left == b.header_left && a.clock_caption == b.clock_caption &&
         a.clock_hhmm == b.clock_hhmm && a.total_odo_value == b.total_odo_value &&
         a.total_odo == b.total_odo && a.active_badge == b.active_badge &&
         a.summary_left == b.summary_left && a.summary_right == b.summary_right &&
         a.cells == b.cells && a.month_count == b.month_count &&
         std::equal(a.months.begin(), a.months.begin() + a.month_count,
                    b.months.begin()) &&
         a.milestone_label == b.milestone_label &&
         a.milestone_pct == b.milestone_pct && a.footer_chip == b.footer_chip &&
         a.footer_text == b.footer_text;
}

MonumentView derive_monument_view(const MonumentSnapshot& monument,
                                  const TokenSnapshot& token, uint64_t now,
                                  const TokenSync& sync,
                                  RuntimeMode runtime_mode);
std::string encode_monument_view(const MonumentView& view);
bool decode_monument_view(const std::string& json, MonumentView& out);
PageRecord monument_page(const MonumentSnapshot& monument,
                         const TokenSnapshot& token, uint64_t now,
                         const TokenSync& sync, RuntimeMode runtime_mode);
bool update_monument_page(DeviceState& device, const MonumentSnapshot& monument,
                          const TokenSnapshot& token, uint64_t now,
                          const TokenSync& sync, RuntimeMode runtime_mode);

}  // namespace rcpaper
