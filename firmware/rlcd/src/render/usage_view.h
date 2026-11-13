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
// Each provider is a car on a road of fuel. The car sits at the share of the
// weekly allowance already used, so the solid road ahead of it is what is left
// (the big number) and the pump at the end means empty. A tick under the road
// marks even pace, the share of the 7-day window already gone: a car behind the
// tick has room to spare, a car past it is burning fast.

constexpr int16_t kUsageRoadPx = 150;   // road length in pixels (0 % used .. empty)
constexpr uint64_t kUsageWeekS = 604800;

enum class UsageMood : uint8_t { None, Open, Comfy, Pace, Hot, Low, Nap };
enum class UsageRowState : uint8_t { Unavailable, Live, Stale };
enum class UsageIcon : uint8_t { None, Spark, Clock, Face };

struct UsageRowView {
  std::string id;       // payload provider id: logo, name and merge identity
  UsageRowState state = UsageRowState::Unavailable;
  uint8_t left = 0;           // displayed remaining percent (used rounded once)
  UsageMood mood = UsageMood::None;
  int16_t car_px = -1;        // used share of the road, -1 without data
  int16_t pace_px = -1;       // even-pace tick (elapsed share of the week), -1 when unknown
  bool runs_dry = false;      // this week's burn rate empties it before the reset
  std::string countdown = "--";
  std::string verdict;        // "dry 25h early", "~91% to spare", "out of gas", ...
  std::string chip;           // row-specific staleness label, e.g. "3h old"
};

struct UsageView {
  std::array<UsageRowView, kUsageMaxProviders> rows;
  size_t count = 0;     // payload order; the renderer draws exactly these rows
  UsageIcon headline_icon = UsageIcon::None;
  UsageMood headline_face = UsageMood::None;
  std::string headline;       // text before the optional pick logo/name
  int8_t pick = -1;           // provider recommended in the headline
  std::string footer_chip;    // inverted footer label, empty when all is well
  std::string footer_text;
};

inline bool operator==(const UsageRowView& a, const UsageRowView& b) {
  return a.id == b.id && a.state == b.state && a.left == b.left && a.mood == b.mood &&
         a.car_px == b.car_px && a.pace_px == b.pace_px && a.runs_dry == b.runs_dry &&
         a.countdown == b.countdown && a.verdict == b.verdict && a.chip == b.chip;
}
inline bool operator==(const UsageView& a, const UsageView& b) {
  return a.count == b.count && a.rows == b.rows && a.headline_icon == b.headline_icon &&
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
