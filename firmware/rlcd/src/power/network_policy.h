#pragma once

#include <cstdint>

#include "power/runtime_policy.h"

namespace rcpaper {

constexpr uint32_t kHostRefreshIntervalSec = 15 * 60;

struct NetworkRefreshQuery {
  uint64_t now_s;
  uint64_t last_fetch_s;
  uint64_t retry_not_before_s;
  bool cache_empty;
  bool clock_valid;
  uint32_t interval_sec;
};

inline uint32_t network_refresh_interval_sec(
    RuntimeMode mode, uint32_t adaptive_interval_sec) {
  return mode == RuntimeMode::Host ? kHostRefreshIntervalSec
                                   : adaptive_interval_sec;
}

inline uint32_t network_failure_retry_sec(
    RuntimeMode mode, uint32_t adaptive_interval_sec) {
  return network_refresh_interval_sec(mode, adaptive_interval_sec);
}

inline bool network_refresh_due(const NetworkRefreshQuery& query) {
  if (query.now_s < query.retry_not_before_s) return false;
  return query.cache_empty || !query.clock_valid || query.last_fetch_s == 0 ||
         (query.now_s >= query.last_fetch_s &&
          query.now_s - query.last_fetch_s >= query.interval_sec);
}

}  // namespace rcpaper
