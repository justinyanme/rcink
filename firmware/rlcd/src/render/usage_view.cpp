#include "render/usage_view.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace rcpaper {

const char* const kUsageProviderNames[4] = {"Claude", "Codex", "Kimi", "Gemini"};

namespace {
constexpr uint64_t kClockMinimum = 1704067200;
constexpr size_t kMaxLabel = 40;
constexpr const char* kViewSchema = "rcink.usage-view.v2";

std::string local_hhmm(uint64_t epoch_s) {
  const time_t instant = (time_t)epoch_s;
  struct tm local = {};
  localtime_r(&instant, &local);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", local.tm_hour, local.tm_min);
  return buf;
}

// Hour granularity keeps the frame stable for an hour at a time; minutes only
// appear in the final hour before a reset.
std::string countdown_label(uint64_t seconds) {
  const unsigned long long hours = seconds / 3600;
  char buf[16];
  if (hours >= 24) std::snprintf(buf, sizeof(buf), "%llud %lluh", hours / 24, hours % 24);
  else if (hours >= 1) std::snprintf(buf, sizeof(buf), "%lluh", hours);
  else std::snprintf(buf, sizeof(buf), "%llum", std::max<unsigned long long>(1, seconds / 60));
  return buf;
}

std::string age_label(uint64_t seconds) {
  char buf[16];
  if (seconds < 3600) std::snprintf(buf, sizeof(buf), "%llum old", std::max<unsigned long long>(1, seconds / 60));
  else if (seconds < 86400) std::snprintf(buf, sizeof(buf), "%lluh old", (unsigned long long)(seconds / 3600));
  else std::snprintf(buf, sizeof(buf), "%llud old", (unsigned long long)(seconds / 86400));
  return buf;
}

// Remaining allowance compared with the even-pace share of the week still left.
UsageMood mood_for(int left, double pace) {
  if (left == 0) return UsageMood::Nap;
  if (left <= 10) return UsageMood::Low;
  if (pace < 0) return UsageMood::None;
  const double headroom = left - pace;
  if (headroom < -8) return UsageMood::Hot;
  if (headroom <= 8) return UsageMood::Pace;
  if (headroom <= 40) return UsageMood::Comfy;
  return UsageMood::Open;
}

// Project this week's average burn rate forward to the reset.
void project(UsageRowView& row, int used, double pace) {
  const double elapsed = 1.0 - pace / 100.0;
  if (row.left == 0) { row.runs_dry = true; row.verdict = "out of gas"; return; }
  if (elapsed <= 0.02) { row.verdict = "new week"; return; }
  const double rate = used / elapsed;  // percent per week
  const double empty_at = rate > 0 ? elapsed + row.left / rate : 2.0;
  const double hours_early = (1.0 - empty_at) * 168.0;
  char buf[24];
  if (hours_early >= 1.0) {
    row.runs_dry = true;
    const long hours = std::lround(hours_early);
    if (hours >= 48) std::snprintf(buf, sizeof(buf), "dry %ldd early", std::lround(hours / 24.0));
    else std::snprintf(buf, sizeof(buf), "dry %ldh early", hours);
  } else {
    const long spare = std::max(0L, std::lround(row.left - rate * (1.0 - elapsed)));
    std::snprintf(buf, sizeof(buf), "~%ld%% to spare", spare);
  }
  row.verdict = buf;
}

bool small_string(JsonVariantConst value, std::string& out) {
  if (!value.is<const char*>()) return false;
  out = value.as<const char*>();
  return out.size() <= kMaxLabel;
}

bool ranged(JsonVariantConst value, int lo, int hi, int& out) {
  if (!value.is<int>()) return false;
  out = value.as<int>();
  return out >= lo && out <= hi;
}
}  // namespace

UsageView derive_usage_view(const UsageSnapshot& snapshot, uint64_t now,
                            const UsageSync& sync) {
  UsageView view;
  const bool clock = now >= kClockMinimum;
  double pace[4] = {-1, -1, -1, -1};
  uint64_t newest_sample = 0;
  bool any = false, all_stale = true;

  for (size_t i = 0; i < 4; i++) {
    const UsageProvider& p = snapshot.providers[i];
    UsageRowView& row = view.rows[i];
    if (p.status == UsageStatus::Unavailable) { row.verdict = "no data yet"; continue; }
    any = true;
    newest_sample = std::max(newest_sample, p.fetched_at);
    const int used = std::min(100, std::max(0, (int)std::round(p.used_percent)));
    row.left = (uint8_t)(100 - used);
    row.car_px = (int16_t)std::lround(used * kUsageRoadPx / 100.0);
    const bool stale = p.status == UsageStatus::Stale || !clock || now + 300 < p.fetched_at ||
        now >= p.expires_at || (p.resets_at != 0 && now >= p.resets_at);
    row.state = stale ? UsageRowState::Stale : UsageRowState::Live;
    if (!stale) all_stale = false;
    if (clock && p.resets_at != 0) {
      if (p.resets_at > now) {
        const uint64_t remaining = p.resets_at - now;
        const double share_left = std::min(1.0, (double)remaining / kUsageWeekS);
        pace[i] = share_left * 100.0;
        row.pace_px = (int16_t)std::lround((1.0 - share_left) * kUsageRoadPx);
        row.countdown = countdown_label(remaining);
      } else {
        row.pace_px = kUsageRoadPx;
        row.countdown = "due";
      }
    }
    if (stale) continue;  // no moods or projections from numbers we no longer trust
    row.mood = mood_for(row.left, pace[i]);
    if (pace[i] >= 0) project(row, used, pace[i]);
  }
  all_stale = any && all_stale;

  // Name the odd one out; when everything is stale the footer says it once.
  for (size_t i = 0; i < 4; i++) {
    UsageRowView& row = view.rows[i];
    if (row.state != UsageRowState::Stale || all_stale) continue;
    const uint64_t fetched = snapshot.providers[i].fetched_at;
    row.chip = clock && now + 300 >= fetched ? age_label(now > fetched ? now - fetched : 0) : "stale";
  }

  if (!clock) {
    view.headline_icon = UsageIcon::Clock;
    view.headline = "What day is it? Clock not set.";
  } else if (!any) {
    view.headline_icon = UsageIcon::Face;
    view.headline_face = UsageMood::Pace;
    view.headline = "Waiting for the first numbers.";
  } else if (all_stale) {
    // The device can only see that the source has gone quiet, not why.
    view.headline_icon = sync.fetch_failed ? UsageIcon::Face : UsageIcon::Clock;
    view.headline_face = sync.fetch_failed ? UsageMood::Low : UsageMood::None;
    view.headline = (sync.fetch_failed ? "No signal. Numbers from " : "No new numbers since ") +
                    local_hhmm(newest_sample) + ".";
  } else {
    int pick = -1, nap = -1, low = -1;
    double best = -1e9;
    for (int i = 0; i < 4; i++) {
      const UsageRowView& row = view.rows[i];
      if (row.state != UsageRowState::Live) continue;
      if (row.mood == UsageMood::Nap && nap < 0) nap = i;
      if (row.mood == UsageMood::Low && low < 0) low = i;
      if (row.left > 10 && pace[i] >= 0 && row.left - pace[i] > best) {
        best = row.left - pace[i];
        pick = i;
      }
    }
    view.headline_icon = UsageIcon::Face;
    if (pick >= 0 && nap >= 0) {
      view.headline_face = UsageMood::Nap;
      view.headline = std::string(kUsageProviderNames[nap]) + " is napping. Try";
    } else if (pick >= 0 && low >= 0) {
      view.headline_face = UsageMood::Low;
      view.headline = std::string(kUsageProviderNames[low]) + " is almost out. Try";
    } else if (pick >= 0) {
      view.headline_icon = UsageIcon::Spark;
      view.headline = "Best bet today:";
    } else {
      view.headline_face = UsageMood::Pace;
      view.headline = "Nothing fresh to recommend.";
    }
    view.pick = (int8_t)pick;
  }

  if (!clock) {
    view.footer_chip = "NO CLOCK";
    view.footer_text = "waiting for NTP";
  } else if (sync.fetch_failed) {
    view.footer_chip = "SYNC FAILED";
    view.footer_text = sync.retry_at_s > now ? "retry " + local_hhmm(sync.retry_at_s) : "retrying";
  } else if (all_stale) {
    // "Synced" would read as fresh; the GET worked but the numbers are old.
    view.footer_chip = "OLD DATA";
    view.footer_text = sync.last_ok_s ? "checked " + local_hhmm(sync.last_ok_s) : "not synced yet";
  } else if (sync.last_ok_s == 0) {
    view.footer_text = "Not synced yet";
  } else {
    view.footer_text = "Synced " + local_hhmm(sync.last_ok_s);
  }
  return view;
}

std::string encode_usage_view(const UsageView& view) {
  JsonDocument doc;
  doc["schema"] = kViewSchema;
  doc["icon"] = (int)view.headline_icon;
  doc["face"] = (int)view.headline_face;
  doc["headline"] = view.headline;
  doc["pick"] = (int)view.pick;
  doc["footerChip"] = view.footer_chip;
  doc["footer"] = view.footer_text;
  JsonArray rows = doc["rows"].to<JsonArray>();
  for (const UsageRowView& row : view.rows) {
    JsonObject o = rows.add<JsonObject>();
    o["state"] = (int)row.state;
    o["left"] = (int)row.left;
    o["mood"] = (int)row.mood;
    o["car"] = (int)row.car_px;
    o["pace"] = (int)row.pace_px;
    o["dry"] = row.runs_dry;
    o["countdown"] = row.countdown;
    o["verdict"] = row.verdict;
    o["chip"] = row.chip;
  }
  std::string json;
  serializeJson(doc, json);
  return json;
}

bool decode_usage_view(const std::string& json, UsageView& out) {
  JsonDocument doc;
  if (json.size() > 2048 || deserializeJson(doc, json)) return false;
  JsonVariantConst root = doc.as<JsonVariantConst>();
  if (root["schema"] != kViewSchema || !root["rows"].is<JsonArrayConst>() ||
      root["rows"].size() != 4) return false;
  UsageView next;
  int value = 0;
  if (!ranged(root["icon"], 0, (int)UsageIcon::Face, value)) return false;
  next.headline_icon = (UsageIcon)value;
  if (!ranged(root["face"], 0, (int)UsageMood::Nap, value)) return false;
  next.headline_face = (UsageMood)value;
  if (!ranged(root["pick"], -1, 3, value)) return false;
  next.pick = (int8_t)value;
  if (!small_string(root["headline"], next.headline) ||
      !small_string(root["footerChip"], next.footer_chip) ||
      !small_string(root["footer"], next.footer_text)) return false;
  for (size_t i = 0; i < 4; i++) {
    JsonVariantConst o = root["rows"][i];
    UsageRowView& row = next.rows[i];
    if (!ranged(o["state"], 0, (int)UsageRowState::Stale, value)) return false;
    row.state = (UsageRowState)value;
    if (!ranged(o["left"], 0, 100, value)) return false;
    row.left = (uint8_t)value;
    if (!ranged(o["mood"], 0, (int)UsageMood::Nap, value)) return false;
    row.mood = (UsageMood)value;
    if (!ranged(o["car"], -1, kUsageRoadPx, value)) return false;
    row.car_px = (int16_t)value;
    if (!ranged(o["pace"], -1, kUsageRoadPx, value)) return false;
    row.pace_px = (int16_t)value;
    if (!o["dry"].is<bool>()) return false;
    row.runs_dry = o["dry"].as<bool>();
    if (!small_string(o["countdown"], row.countdown) ||
        !small_string(o["verdict"], row.verdict) ||
        !small_string(o["chip"], row.chip)) return false;
  }
  out = next;
  return true;
}

}  // namespace rcpaper
