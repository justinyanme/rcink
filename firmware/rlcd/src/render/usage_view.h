#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "render/usage.h"

namespace rcpaper {

// Display model for the "road" weekly-allowance screen. Everything the frame
// shows is derived here, purely, so native tests cover it and the encoded form
// doubles as the redraw fingerprint: if this model is unchanged, so is the frame.
//
// Each provider is a car on a road whose finish flag is that provider's weekly
// reset. The car sits at the elapsed share of the 7-day window; the solid road
// ahead is how far this week's average burn rate lasts.

constexpr int16_t kUsageRoadPx = 150;   // road length in pixels (week start .. reset)
constexpr uint64_t kUsageWeekS = 604800;

extern const char* const kUsageProviderNames[4];

enum class UsageMood : uint8_t { None, Open, Comfy, Pace, Hot, Low, Nap };
enum class UsageRowState : uint8_t { Unavailable, Live, Stale };
enum class UsageIcon : uint8_t { None, Spark, Clock, Face };

struct UsageRowView {
  UsageRowState state = UsageRowState::Unavailable;
  uint8_t left = 0;           // displayed remaining percent (used rounded once)
  UsageMood mood = UsageMood::None;
  int16_t now_px = -1;        // car position, -1 when the week position is unknown
  int16_t dry_px = -1;        // projected empty point, -1 when it lasts to the reset
  std::string countdown = "--";
  std::string verdict;        // "dry 25h early", "~91% to spare", "out of gas", ...
  std::string chip;           // row-specific staleness label, e.g. "3h old"
};

struct UsageView {
  std::array<UsageRowView, 4> rows;
  UsageIcon headline_icon = UsageIcon::None;
  UsageMood headline_face = UsageMood::None;
  std::string headline;       // text before the optional pick logo/name
  int8_t pick = -1;           // provider recommended in the headline
  std::string footer_chip;    // inverted footer label, empty when all is well
  std::string footer_text;
};

inline bool operator==(const UsageRowView& a, const UsageRowView& b) {
  return a.state == b.state && a.left == b.left && a.mood == b.mood &&
         a.now_px == b.now_px && a.dry_px == b.dry_px &&
         a.countdown == b.countdown && a.verdict == b.verdict && a.chip == b.chip;
}
inline bool operator==(const UsageView& a, const UsageView& b) {
  return a.rows == b.rows && a.headline_icon == b.headline_icon &&
         a.headline_face == b.headline_face && a.headline == b.headline &&
         a.pick == b.pick && a.footer_chip == b.footer_chip &&
         a.footer_text == b.footer_text;
}

// `now` below 2024 means the wall clock is untrustworthy. Local HH:MM labels use
// the process TZ (LOCAL_TZ on the device).
UsageView derive_usage_view(const UsageSnapshot& snapshot, uint64_t now,
                            const UsageSync& sync);
std::string encode_usage_view(const UsageView& view);
bool decode_usage_view(const std::string& json, UsageView& out);

}  // namespace rcpaper
