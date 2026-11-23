#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "proto/envelope.h"
#include "render/link_status.h"
#include "store/nvs.h"

namespace rcpaper {

constexpr size_t kUsageMaximumBytes = 4096;
constexpr size_t kUsageMaxProviders = 4;  // what the 400x300 frame can draw
constexpr size_t kUsageMaxIdLength = 16;
enum class UsageStatus : uint8_t { Unavailable, Ok, Stale };
struct UsageProvider {
  std::string id;  // stable account id from the source; defines row identity
  UsageStatus status = UsageStatus::Unavailable;
  double used_percent = 0;
  uint64_t fetched_at = 0;
  uint64_t expires_at = 0;
  uint64_t resets_at = 0;  // 0 means the source supplied null.
};
struct UsageSnapshot {
  // The screen mirrors exactly the accounts the source reports, in payload
  // order: an account removed upstream drops out here instead of lingering.
  std::array<UsageProvider, kUsageMaxProviders> providers;
  size_t count = 0;
  uint64_t served_at = 0;
};

// Display name for a payload id. Known ids keep their established names;
// anything else is shown capitalized so new accounts read fine unconfigured.
std::string usage_provider_name(const std::string& id);

// Device-side sync facts the screen reports alongside the source snapshot.
// A plain aggregate (the ESP32 toolchain is C++11): `{}` means never synced.
struct UsageSync {
  bool fetch_failed;
  uint64_t last_ok_s;   // completion time of the last successful GET, 0 if none
  uint64_t retry_at_s;  // next scheduled attempt after a failure, 0 if unknown
  bool syncing;         // a sync the user asked for (BOOT) is in flight
  LinkFailure link_failure;
  uint32_t retry_cooldown_s;  // static cooldown for no-clock retry hint
  const char* wifi_ssid;      // configured SSID shown on WifiFailed
};

constexpr const char* kUsagePageId = "usage-limits";

// Validate the complete response before replacing the caller's cache. The
// providers array is the source's account list: 0..4 rows, unique ids, any
// order; ids absent from later payloads are dropped at merge time.
bool decode_usage(const std::string& json, UsageSnapshot& out,
                  std::string* reason = nullptr);
std::string encode_usage(const UsageSnapshot& snapshot);
bool merge_usage(const UsageSnapshot& incoming, UsageSnapshot& cache);
// The usage page is hand-drawn, not DSL: its layout_json carries the encoded
// UsageView (render/usage_view.h), which is both the frame source and the
// redraw fingerprint.
PageRecord usage_page(const UsageSnapshot& snapshot, uint64_t now, const UsageSync& sync);
bool update_usage_page(DeviceState& device, const UsageSnapshot& snapshot,
                       uint64_t now, const UsageSync& sync);
uint32_t usage_page_fingerprint(const PageRecord& page);

// Source samples expire ten minutes after they are taken, while the source,
// its uploader and this device each run independent schedules. With a USB host,
// pre-fetch 60 s before the earliest fresh sample expires (clamped to at least
// kUsageChaseRetryS after last_fetch_s so a late source cannot spin); if a
// sample is already past expiry, keep looking every minute until half an hour
// after its sample. Returns 0 when nothing calls for an early fetch.
constexpr uint64_t kUsagePreExpiryLeadS = 60;
constexpr uint64_t kUsageChaseRetryS = 60;
constexpr uint64_t kUsageChaseWindowS = 30 * 60;
uint64_t usage_refetch_at_s(const UsageSnapshot& snapshot, uint64_t last_fetch_s);

}  // namespace rcpaper
