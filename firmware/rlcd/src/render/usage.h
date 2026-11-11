#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "proto/envelope.h"
#include "store/nvs.h"

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

// Device-side sync facts the screen reports alongside the source snapshot.
// A plain aggregate (the ESP32 toolchain is C++11): `{}` means never synced.
struct UsageSync {
  bool fetch_failed;
  uint64_t last_ok_s;   // completion time of the last successful GET, 0 if none
  uint64_t retry_at_s;  // next scheduled attempt after a failure, 0 if unknown
};

constexpr const char* kUsagePageId = "usage-limits";

// Validate the complete four-slot response before replacing the caller's cache.
bool decode_usage(const std::string& json, UsageSnapshot& out);
std::string encode_usage(const UsageSnapshot& snapshot);
bool merge_usage(const UsageSnapshot& incoming, UsageSnapshot& cache);
// The usage page is hand-drawn, not DSL: its layout_json carries the encoded
// UsageView (render/usage_view.h), which is both the frame source and the
// redraw fingerprint.
PageRecord usage_page(const UsageSnapshot& snapshot, uint64_t now, const UsageSync& sync);
bool update_usage_page(DeviceState& device, const UsageSnapshot& snapshot,
                       uint64_t now, const UsageSync& sync);
uint32_t usage_page_fingerprint(const PageRecord& page);

}  // namespace rcpaper
