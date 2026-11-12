#pragma once

#include <cstdint>

#include "power/runtime_policy.h"

namespace rcpaper {

constexpr uint32_t kHostRefreshIntervalSec = 15 * 60;
constexpr uint32_t kUsageHostRefreshIntervalSec = 5 * 60;
// A powered host retries a failed usage GET quickly: the device's path to the
// Worker sometimes times out, and a full poll would leave expired numbers up.
constexpr uint32_t kUsageHostFailureRetrySec = 60;
constexpr uint32_t kTokenHostRefreshIntervalSec = 15 * 60;
constexpr uint32_t kTokenBatteryRefreshIntervalSec = 60 * 60;

struct NetworkRefreshQuery {
  uint64_t now_s;
  uint64_t last_fetch_s;
  uint64_t retry_not_before_s;
  bool cache_empty;
  bool clock_valid;
  uint32_t interval_sec;
  bool last_attempt_failed;  // then retry_not_before_s alone schedules the retry
};

inline uint32_t network_refresh_interval_sec(
    RuntimeMode mode, uint32_t adaptive_interval_sec, bool usage_enabled = false) {
  if (mode != RuntimeMode::Host) return adaptive_interval_sec;
  return usage_enabled ? kUsageHostRefreshIntervalSec : kHostRefreshIntervalSec;
}

inline uint32_t network_failure_retry_sec(
    RuntimeMode mode, uint32_t adaptive_interval_sec, bool usage_enabled = false) {
  if (mode == RuntimeMode::Host && usage_enabled) return kUsageHostFailureRetrySec;
  return network_refresh_interval_sec(mode, adaptive_interval_sec, usage_enabled);
}

// Token rides in Allowance worker runs, but has its own success clock. Battery
// modes fetch no more often than hourly; a failed attempt becomes eligible at
// the shared retry gate regardless of the last successful Token response.
inline bool token_refresh_due(uint64_t now_s, uint64_t last_ok_s,
                              uint64_t retry_at_s, RuntimeMode mode,
                              bool last_attempt_failed) {
  if (last_attempt_failed) return now_s >= retry_at_s;
  if (last_ok_s == 0) return true;
  if (now_s < last_ok_s) return false;
  const uint32_t interval = mode == RuntimeMode::Host
      ? kTokenHostRefreshIntervalSec : kTokenBatteryRefreshIntervalSec;
  return now_s - last_ok_s >= interval;
}

inline bool network_refresh_due(const NetworkRefreshQuery& query) {
  if (query.now_s < query.retry_not_before_s) return false;
  // The failure gate can be shorter than the poll interval (powered usage host),
  // and the screen promises a retry at that gate.
  if (query.last_attempt_failed) return true;
  return query.cache_empty || !query.clock_valid || query.last_fetch_s == 0 ||
         (query.now_s >= query.last_fetch_s &&
          query.now_s - query.last_fetch_s >= query.interval_sec);
}

}  // namespace rcpaper
