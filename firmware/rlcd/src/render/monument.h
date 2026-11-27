#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "proto/envelope.h"
#include "render/token.h"
#include "store/nvs.h"

namespace rcpaper {

constexpr size_t kMonumentDays = 365;
constexpr size_t kMonumentMaximumBytes = 24576;
constexpr const char* kMonumentPageId = "year-monument";

enum class HeatmapWindow : uint8_t {
  Year = 0,         // 1Y: 365 days = 52*7 + 1, 53 cols (default)
  SixMonths = 1,    // 6M: 183 days = 26*7 + 1, 27 cols
  ThreeMonths = 2,  // 3M:  92 days = 13*7 + 1, 14 cols
};

inline HeatmapWindow heatmap_window_from_u8(uint8_t raw) {
  switch (raw) {
    case 1:  return HeatmapWindow::SixMonths;
    case 2:  return HeatmapWindow::ThreeMonths;
    default: return HeatmapWindow::Year;
  }
}

inline uint8_t heatmap_window_to_u8(HeatmapWindow window) {
  switch (window) {
    case HeatmapWindow::SixMonths:   return 1;
    case HeatmapWindow::ThreeMonths: return 2;
    case HeatmapWindow::Year:
    default:                         return 0;
  }
}

inline HeatmapWindow next_heatmap_window(HeatmapWindow window) {
  switch (window) {
    case HeatmapWindow::Year:        return HeatmapWindow::SixMonths;
    case HeatmapWindow::SixMonths:   return HeatmapWindow::ThreeMonths;
    case HeatmapWindow::ThreeMonths:
    default:                         return HeatmapWindow::Year;
  }
}

inline const char* heatmap_window_label(HeatmapWindow window) {
  switch (window) {
    case HeatmapWindow::SixMonths:   return "6M";
    case HeatmapWindow::ThreeMonths: return "3M";
    case HeatmapWindow::Year:
    default:                         return "1Y";
  }
}

inline size_t heatmap_window_days(HeatmapWindow window) {
  switch (window) {
    case HeatmapWindow::SixMonths:   return 183;  // 26*7 + 1
    case HeatmapWindow::ThreeMonths: return 92;   // 13*7 + 1
    case HeatmapWindow::Year:
    default:                         return 365;  // 52*7 + 1
  }
}

inline size_t heatmap_window_cols(HeatmapWindow window) {
  switch (window) {
    case HeatmapWindow::SixMonths:   return 27;
    case HeatmapWindow::ThreeMonths: return 14;
    case HeatmapWindow::Year:
    default:                         return 53;
  }
}

struct MonumentSnapshot {
  std::string start_date;
  std::string end_date;
  // Heap-backed so ESP32-S3 tasks don't put 2.9 KB per copy onto loopTask stack.
  std::vector<uint64_t> days = std::vector<uint64_t>(kMonumentDays, 0);
};

enum class TokenWork : uint8_t { None, Queued, InFlight };

struct MonumentRetry {
  bool failed = false;
  TokenWork work = TokenWork::None;  // Token and annual history share one worker
  uint64_t at_s = 0;                // known start of a future eligible worker
};

bool decode_monument(const std::string& json, MonumentSnapshot& out,
                     std::string* reason = nullptr);
std::string encode_monument(const MonumentSnapshot& snapshot);
bool monument_content_equal(const MonumentSnapshot& a,
                            const MonumentSnapshot& b);

}  // namespace rcpaper
