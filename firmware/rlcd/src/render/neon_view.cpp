#include "render/neon_view.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

#include "sense/clock.h"

namespace rcpaper {
namespace {

constexpr const char* kSchema = "rcink.neon-view.v1";
constexpr const char* kWeekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

bool read_string(JsonVariantConst field, size_t max_size, std::string& out) {
  JsonString s = field.as<JsonString>();
  if (s.isNull() || s.size() > max_size || strlen(s.c_str()) != s.size()) return false;
  out = s.c_str();
  return true;
}

}  // namespace

NeonView derive_neon_view(const UsageSnapshot& usage, UsageSync usage_sync,
                          const TokenSnapshot& token, const MonumentSnapshot& monument,
                          TokenSync token_sync, MonumentRetry retry, uint64_t now,
                          const Shtc3Reading& env, const BatteryReading& battery,
                          bool cat_enabled) {
  NeonView v;
  const time_t instant = static_cast<time_t>(now);
  std::tm local{};
  if (wall_clock_is_valid(now) && localtime_r(&instant, &local)) {
    char clock[6];
    std::strftime(clock, sizeof(clock), "%H:%M", &local);
    v.clock_hhmm = clock;
    v.weekday = kWeekdays[local.tm_wday];
    v.day = local.tm_mday;
  }
  if (env.ok && std::isfinite(env.temp_c)) {
    char temp[16];
    std::snprintf(temp, sizeof(temp), "%.1f", env.temp_c);
    v.env_temp = temp;
  }
  if (retry.work == TokenWork::None) usage_sync.syncing = token_sync.syncing = false;
  const auto allowance = derive_usage_view(usage, now, usage_sync);
  for (size_t i = 0; i < allowance.count; ++i) {
    const auto& row = allowance.rows[i];
    const char initial = row.id == "codex" ? 'X' : usage_provider_name(row.id).front();
    v.providers.push_back({initial, row.state == UsageRowState::Unavailable ? -1 : row.left,
                           station_low_fuel(row)});
  }
  bool has_data;
  const auto days = derive_monument_shifted_days(monument, token, now, has_data);
  v.skyline_bars = derive_token_skyline(days, has_data, SkylineScale::EightWeeks).bar_heights;
  if (has_data) v.total_odo = format_tokens(total_odometer(token, days));
  v.battery_pct = battery.pct;
  v.battery_low = !battery.ok || battery.pct < 20;
  v.cat_enabled = cat_enabled;
  v.outage = usage_sync.link_failure != LinkFailure::None ||
             token_sync.link_failure != LinkFailure::None;
  // Annual history rides the Token GET. Its failed response stays visible
  // until the next history attempt, even if an Allowance-only poll succeeds.
  if (retry.failed && !v.outage) {
    token_sync.fetch_failed = true;
    token_sync.retry_at_s = retry.at_s;
  }
  derive_station_footer(allowance, usage_sync, token, token_sync, now, v.footer_chip, v.footer_text);
  if (v.footer_chip == "SYNCING" && retry.work == TokenWork::Queued) {
    v.footer_text = "next sync";
  } else if (retry.failed && !retry.at_s &&
             (v.footer_chip == "TOKEN FAIL" || v.footer_chip == "SYNC FAILED")) {
    v.footer_text = retry.work == TokenWork::InFlight ? "retrying" : "next sync";
  }
  if (v.footer_chip.empty()) {
    v.footer_text.clear();  // L1 has no healthy footer or last-sync timestamp.
    if (v.battery_low) v.footer_chip = "BATTERY " + std::to_string(battery.pct) + "%";
  }
  return v;
}

std::string encode_neon_view(const NeonView& v) {
  JsonDocument doc;
  doc["schema"] = kSchema;
  doc["clock_hhmm"] = v.clock_hhmm;
  doc["weekday"] = v.weekday;
  doc["day"] = v.day;
  doc["env_temp"] = v.env_temp;
  auto rows = doc["providers"].to<JsonArray>();
  for (const auto& p : v.providers) {
    auto row = rows.add<JsonObject>();
    row["id_initial"] = std::string(1, p.id_initial);
    if (p.left < 0) row["left"] = nullptr;
    else row["left"] = p.left;
    row["low_fuel"] = p.low_fuel;
  }
  auto bars = doc["skyline_bars"].to<JsonArray>();
  for (auto h : v.skyline_bars) bars.add(h);
  doc["total_odo"] = v.total_odo;
  doc["battery_pct"] = v.battery_pct;
  doc["battery_low"] = v.battery_low;
  doc["outage"] = v.outage;
  doc["footer_chip"] = v.footer_chip;
  doc["footer_text"] = v.footer_text;
  doc["cat_enabled"] = v.cat_enabled;
  std::string json;
  serializeJson(doc, json);
  return json;
}

bool decode_neon_view(const std::string& json, NeonView& out) {
  if (json.size() > 1536) return false;
  std::istringstream input(json);
  JsonDocument doc;
  if (deserializeJson(doc, input) || !(input >> std::ws).eof() || doc["schema"] != kSchema)
    return false;
  NeonView v;
  if (!read_string(doc["clock_hhmm"], 5, v.clock_hhmm) || v.clock_hhmm.size() != 5 ||
      v.clock_hhmm[2] != ':' || !read_string(doc["weekday"], 3, v.weekday) ||
      !doc["day"].is<uint8_t>() || doc["day"].as<int>() > 31 ||
      !read_string(doc["env_temp"], 6, v.env_temp) ||
      !read_string(doc["total_odo"], 7, v.total_odo) ||
      !read_string(doc["footer_chip"], 16, v.footer_chip) ||
      !read_string(doc["footer_text"], 24, v.footer_text) ||
      !doc["battery_pct"].is<uint8_t>() || doc["battery_pct"].as<int>() > 100 ||
      !doc["battery_low"].is<bool>() || !doc["outage"].is<bool>() ||
      !doc["cat_enabled"].is<bool>()) return false;
  v.day = doc["day"];
  if (v.clock_hhmm == "--:--") {
    if (v.day != 0 || !v.weekday.empty()) return false;
  } else {
    const auto& clock = v.clock_hhmm;
    if (clock.find_first_not_of("0123456789", 3) != std::string::npos ||
        clock.substr(0, 2).find_first_not_of("0123456789") != std::string::npos ||
        clock.substr(0, 2) > "23" || clock.substr(3) > "59" || v.day == 0 ||
        std::find(std::begin(kWeekdays), std::end(kWeekdays), v.weekday) == std::end(kWeekdays))
      return false;
  }
  const auto bars = doc["skyline_bars"].as<JsonArrayConst>();
  const auto rows = doc["providers"].as<JsonArrayConst>();
  if (bars.size() != v.skyline_bars.size() || rows.isNull() || rows.size() > kUsageMaxProviders)
    return false;
  size_t i = 0;
  for (auto h : bars) {
    if (!h.is<uint8_t>() || h.as<int>() > kStationSkylineMaxBarH) return false;
    v.skyline_bars[i++] = h.as<uint8_t>();
  }
  for (JsonObjectConst row : rows) {
    std::string initial;
    if (!read_string(row["id_initial"], 1, initial) || initial.empty() ||
        !row["low_fuel"].is<bool>() || !row["left"].is<JsonVariantConst>()) return false;
    const auto left = row["left"];
    if (!left.isNull() && (!left.is<uint8_t>() || left.as<int>() > 100)) return false;
    const bool low = row["low_fuel"];
    if (low && (left.isNull() || left.as<int>() > 10)) return false;
    v.providers.push_back({initial[0], left.isNull() ? -1 : left.as<int>(), low});
  }
  v.battery_pct = doc["battery_pct"];
  v.battery_low = doc["battery_low"];
  v.outage = doc["outage"];
  v.cat_enabled = doc["cat_enabled"];
  out = std::move(v);
  return true;
}

bool update_neon_page(PageRecord& page, const NeonView& view, uint64_t now) {
  auto json = encode_neon_view(view);
  if (page.id == kNeonPageId && page.layout_json == json) return false;
  page = {kNeonPageId, 4, now, std::move(json)};
  return true;
}

}  // namespace rcpaper
