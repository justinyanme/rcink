#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "proto/envelope.h"

namespace rcpaper {

constexpr size_t kUsageMaximumBytes = 4096;
enum class UsageStatus : uint8_t { Unavailable, Ok, Stale };
struct UsageProvider {
  UsageStatus status = UsageStatus::Unavailable;
  double used_percent = 0;
  uint64_t fetched_at = 0;
  uint64_t expires_at = 0;
  uint64_t resets_at = 0;  // 0 means the source supplied null.
};
struct UsageSnapshot {
  std::array<UsageProvider, 4> providers;
  uint64_t served_at = 0;
};

// Validate the complete four-slot response before replacing the caller's cache.
bool decode_usage(const std::string& json, UsageSnapshot& out);
PageRecord usage_page(const UsageSnapshot& snapshot, uint64_t now, bool fetch_failed);

}  // namespace rcpaper
