#pragma once

#include <cstdint>

namespace rcpaper {

inline bool dashboard_sample_due(uint64_t now_s, uint64_t sampled_minute,
                                 bool snapshot_valid) {
  return !snapshot_valid || now_s / 60 != sampled_minute;
}

}  // namespace rcpaper
