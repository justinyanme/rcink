// src/power/refresh_schedule.cpp
#include "power/refresh_schedule.h"
#include <algorithm>

namespace rcpaper {

uint32_t schedule_next_refresh_ms(uint32_t now_ms, uint32_t interval_sec) {
  return now_ms + interval_sec * 1000UL;
}

bool refresh_due_ms(uint32_t now_ms, uint32_t deadline_ms) {
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

uint32_t adaptive_refresh_interval_sec(uint32_t base_interval_sec,
                                       uint8_t unchanged_fetches,
                                       uint8_t battery_pct,
                                       bool battery_ok) {
  const uint8_t capped_streak = std::min<uint8_t>(unchanged_fetches, 2);
  uint32_t interval = base_interval_sec << capped_streak;
  if (battery_ok && battery_pct < 20) interval *= 2;
  return interval;
}

static uint32_t fnv1a_mix(uint32_t hash, uint8_t byte) {
  hash ^= byte;
  return hash * 16777619UL;
}

uint32_t pages_fingerprint(const std::vector<PageRecord>& pages) {
  uint32_t hash = 2166136261UL;
  for (const auto& page : pages) {
    for (char ch : page.id) hash = fnv1a_mix(hash, (uint8_t)ch);
    hash = fnv1a_mix(hash, (uint8_t)page.order);
    for (int shift = 0; shift < 64; shift += 8) {
      hash = fnv1a_mix(hash, (uint8_t)(page.data_ts >> shift));
    }
  }
  return hash == 0 ? 1 : hash;
}

void record_successful_fetch(DeviceState& state, const std::vector<PageRecord>& pages) {
  const uint32_t next_fingerprint = pages_fingerprint(pages);
  if (state.last_fetch_fingerprint != 0 && state.last_fetch_fingerprint == next_fingerprint) {
    if (state.unchanged_fetches < 255) state.unchanged_fetches++;
  } else {
    state.unchanged_fetches = 0;
  }
  state.last_fetch_fingerprint = next_fingerprint;
}

}  // namespace rcpaper
