#include "render/station_view.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace rcpaper {
namespace {

constexpr uint64_t kClockMinimum = 1704067200ULL;
constexpr uint64_t kOldSourceAgeS = 2 * 3600;
constexpr size_t kStationViewMaxBytes = 4096;
constexpr const char* kStationSchema = "rcink.station-view.v1";

std::string local_hhmm(uint64_t epoch_s) {
  const time_t instant = (time_t)epoch_s;
  std::tm local = {};
  localtime_r(&instant, &local);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", local.tm_hour, local.tm_min);
  return buf;
}

bool is_known_token_provider(const std::string& id) {
  return id == "claude" || id == "codex" || id == "gemini";
}

bool small_str(JsonVariantConst v, std::string& out, size_t max_len = 64) {
  if (!v.is<const char*>()) return false;
  out = v.as<const char*>();
  return out.size() <= max_len;
}

bool ranged_int(JsonVariantConst v, int lo, int hi, int& out) {
  if (!v.is<int>()) return false;
  out = v.as<int>();
  return out >= lo && out <= hi;
}

bool valid_window_shape(const std::string& label, uint8_t cols) {
  return (label == "1Y" && cols == 53) || (label == "6M" && cols == 27) ||
         (label == "3M" && cols == 14);
}

}  // namespace

StationView derive_station_view(const UsageSnapshot& usage,
                                const UsageSync& usage_sync,
                                const TokenSnapshot& token,
                                const MonumentSnapshot& monument,
                                const TokenSync& token_sync, uint64_t now,
                                RuntimeMode runtime_mode,
                                HeatmapWindow window) {
  const MonumentView mv = derive_monument_view(monument, token, now, token_sync,
                                               runtime_mode, window);
  const UsageView uv = derive_usage_view(usage, now, usage_sync);
  const TokenView tv = derive_token_view(token, now, token_sync);

  StationView view;
  view.header_left = mv.header_left;
  view.eco_last_sync = (runtime_mode == RuntimeMode::Eco);

  const bool clock_valid = now >= kClockMinimum;
  const uint64_t newest_sync_ok =
      std::max(usage_sync.last_ok_s, token_sync.last_ok_s);
  if (clock_valid) {
    if (view.eco_last_sync) {
      view.clock_hhmm = newest_sync_ok >= kClockMinimum
                            ? local_hhmm(newest_sync_ok)
                            : "--:--";
    } else {
      view.clock_hhmm = local_hhmm(now);
    }
  } else {
    view.clock_hhmm = "--:--";
  }

  view.total_odo_value = mv.total_odo_value;
  view.total_odo = mv.total_odo;

  bool has_token_history = false;
  uint64_t today_tokens = 0;
  uint32_t streak_days = 0;
  derive_monument_totals(monument, token, now, has_token_history, today_tokens,
                         streak_days);
  view.today_total = has_token_history ? format_tokens(today_tokens) : "--";
  char streak_buf[16];
  std::snprintf(streak_buf, sizeof(streak_buf), "%ud", (unsigned)streak_days);
  view.streak_label = streak_buf;

  view.window_label = mv.window_label;
  view.cols = mv.cols;
  view.cells = mv.cells;
  view.months = mv.months;
  view.month_count = mv.month_count;

  view.row_count = uv.count;
  bool any_allowance_observed = false;
  bool all_allowance_stale = true;
  for (size_t i = 0; i < uv.count; i++) {
    const UsageRowView& src = uv.rows[i];
    StationRowView& dst = view.rows[i];
    dst.id = src.id;
    dst.state = src.state;
    dst.left = src.left;
    dst.low_fuel = (src.state == UsageRowState::Live && src.left <= 10);
    dst.car_px = src.car_px >= 0
                     ? (int16_t)std::lround((double)src.car_px * kStationBarPx /
                                            kUsageRoadPx)
                     : -1;
    dst.pace_px = src.pace_px >= 0
                      ? (int16_t)std::lround((double)src.pace_px *
                                             kStationBarPx / kUsageRoadPx)
                      : -1;
    dst.countdown = src.countdown;
    dst.chip = src.chip;

    if (src.state != UsageRowState::Unavailable) {
      any_allowance_observed = true;
      if (src.state != UsageRowState::Stale) all_allowance_stale = false;
    }

    int token_idx = -1;
    for (size_t p = 0; p < tv.count; p++) {
      if (tv.rows[p].id == src.id) {
        token_idx = (int)p;
        break;
      }
    }
    if (token_idx >= 0) {
      dst.has_token = true;
      dst.seven_day = tv.rows[(size_t)token_idx].seven_day;
    } else if (is_known_token_provider(src.id)) {
      dst.has_token = true;
      dst.seven_day = token.today.empty() ? "--" : "0";
    } else {
      dst.has_token = false;
      dst.seven_day.clear();
    }
  }
  all_allowance_stale = any_allowance_observed && all_allowance_stale;

  const bool token_old_data =
      !token.today.empty() &&
      (token.required_source_collected_at == 0 ||
       (clock_valid && now > token.required_source_collected_at &&
        now - token.required_source_collected_at > kOldSourceAgeS));

  const LinkFailure link = usage_sync.link_failure != LinkFailure::None
                               ? usage_sync.link_failure
                               : token_sync.link_failure;
  const uint64_t link_retry_at =
      usage_sync.link_failure != LinkFailure::None ? usage_sync.retry_at_s
                                                   : token_sync.retry_at_s;
  const uint32_t link_cooldown =
      usage_sync.link_failure != LinkFailure::None
          ? usage_sync.retry_cooldown_s
          : token_sync.retry_cooldown_s;

  if (usage_sync.syncing || token_sync.syncing) {
    view.footer_chip = "SYNCING";
    view.footer_text = "fetching now";
  } else if (link == LinkFailure::WifiFailed) {
    view.footer_chip = "NO WIFI";
    view.footer_text =
        retry_footer_text(now, clock_valid, link_retry_at, link_cooldown);
  } else if (link == LinkFailure::NtpFailed) {
    view.footer_chip = "NO NTP";
    view.footer_text =
        retry_footer_text(now, clock_valid, link_retry_at, link_cooldown);
  } else if (!clock_valid) {
    view.footer_chip = "NO CLOCK";
    view.footer_text = "waiting for NTP";
  } else if (usage_sync.fetch_failed && token_sync.fetch_failed) {
    const uint64_t retry_at =
        (usage_sync.retry_at_s > 0 && token_sync.retry_at_s > 0)
            ? std::min(usage_sync.retry_at_s, token_sync.retry_at_s)
            : std::max(usage_sync.retry_at_s, token_sync.retry_at_s);
    const uint32_t cooldown =
        std::min(usage_sync.retry_cooldown_s, token_sync.retry_cooldown_s);
    view.footer_chip = "SYNC FAILED";
    view.footer_text = retry_footer_text(now, clock_valid, retry_at, cooldown);
  } else if (usage_sync.fetch_failed) {
    view.footer_chip = "ALLOWANCE";
    view.footer_text = retry_footer_text(
        now, clock_valid, usage_sync.retry_at_s, usage_sync.retry_cooldown_s);
  } else if (token_sync.fetch_failed) {
    view.footer_chip = "TOKEN FAIL";
    view.footer_text = retry_footer_text(
        now, clock_valid, token_sync.retry_at_s, token_sync.retry_cooldown_s);
  } else if (token_old_data) {
    view.footer_chip = "OLD DATA";
    view.footer_text = token.required_source_collected_at == 0
                           ? "M4 --:--"
                           : "M4 " + local_hhmm(token.required_source_collected_at);
  } else if (all_allowance_stale) {
    view.footer_chip = "OLD DATA";
    view.footer_text = usage_sync.last_ok_s
                           ? "checked " + local_hhmm(usage_sync.last_ok_s)
                           : "not synced yet";
  } else if (usage_sync.last_ok_s == 0 && token_sync.last_ok_s == 0) {
    view.footer_text = "Not synced yet";
  } else {
    const uint64_t conservative_ok =
        (usage_sync.last_ok_s > 0 && token_sync.last_ok_s > 0)
            ? std::min(usage_sync.last_ok_s, token_sync.last_ok_s)
            : std::max(usage_sync.last_ok_s, token_sync.last_ok_s);
    view.footer_text = "Synced " + local_hhmm(conservative_ok);
  }

  return view;
}

std::string encode_station_view(const StationView& view) {
  JsonDocument doc;
  doc["schema"] = kStationSchema;
  doc["win"] = view.window_label;
  doc["cols"] = view.cols;
  doc["headerLeft"] = view.header_left;
  doc["ecoSync"] = view.eco_last_sync;
  doc["clock"] = view.clock_hhmm;
  doc["odoVal"] = view.total_odo_value;
  doc["odo"] = view.total_odo;
  doc["today"] = view.today_total;
  doc["streak"] = view.streak_label;
  doc["fChip"] = view.footer_chip;
  doc["fText"] = view.footer_text;

  const size_t active_cells =
      std::min<size_t>((size_t)view.cols * kMonumentRows, view.cells.size());
  std::string cell_str;
  cell_str.reserve(active_cells);
  for (size_t i = 0; i < active_cells; i++) {
    const uint8_t c = view.cells[i];
    cell_str.push_back(c == kMonumentCellOutside ? '.' : (char)('0' + (c & 7)));
  }
  doc["cells"] = cell_str;

  JsonArray months = doc["months"].to<JsonArray>();
  for (size_t i = 0; i < view.month_count; i++) {
    JsonObject m = months.add<JsonObject>();
    m["c"] = view.months[i].col;
    m["l"] = view.months[i].label;
  }

  JsonArray rows = doc["rows"].to<JsonArray>();
  for (size_t i = 0; i < view.row_count; i++) {
    const StationRowView& r = view.rows[i];
    JsonObject o = rows.add<JsonObject>();
    o["id"] = r.id;
    o["st"] = (int)r.state;
    o["left"] = (int)r.left;
    o["low"] = r.low_fuel;
    o["car"] = (int)r.car_px;
    o["pace"] = (int)r.pace_px;
    o["cd"] = r.countdown;
    o["chip"] = r.chip;
    o["tok"] = r.has_token;
    o["s7"] = r.seven_day;
  }

  std::string json;
  serializeJson(doc, json);
  return json;
}

bool decode_station_view(const std::string& json, StationView& out) {
  if (json.empty() || json.size() > kStationViewMaxBytes) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  JsonVariantConst root = doc.as<JsonVariantConst>();
  if (root["schema"] != kStationSchema || !root["cols"].is<uint8_t>() ||
      !root["ecoSync"].is<bool>() || !root["odoVal"].is<uint64_t>() ||
      !root["cells"].is<const char*>() ||
      !root["months"].is<JsonArrayConst>() ||
      !root["rows"].is<JsonArrayConst>()) {
    return false;
  }

  StationView next;
  next.cells.fill(kMonumentCellOutside);
  next.cols = root["cols"].as<uint8_t>();
  next.eco_last_sync = root["ecoSync"].as<bool>();
  next.total_odo_value = root["odoVal"].as<uint64_t>();
  if (!small_str(root["win"], next.window_label, 8) ||
      !valid_window_shape(next.window_label, next.cols) ||
      !small_str(root["headerLeft"], next.header_left) ||
      !small_str(root["clock"], next.clock_hhmm, 12) ||
      !small_str(root["odo"], next.total_odo, 16) ||
      !small_str(root["today"], next.today_total, 16) ||
      !small_str(root["streak"], next.streak_label, 16) ||
      !small_str(root["fChip"], next.footer_chip, 24) ||
      !small_str(root["fText"], next.footer_text)) {
    return false;
  }

  const std::string cell_str = root["cells"].as<const char*>();
  const size_t expected_cells = (size_t)next.cols * kMonumentRows;
  if (cell_str.size() != expected_cells) return false;
  for (size_t i = 0; i < cell_str.size(); i++) {
    const char ch = cell_str[i];
    if (ch == '.') next.cells[i] = kMonumentCellOutside;
    else if (ch >= '0' && ch <= '4') next.cells[i] = (uint8_t)(ch - '0');
    else return false;
  }

  JsonArrayConst months = root["months"].as<JsonArrayConst>();
  if (months.size() > next.months.size()) return false;
  next.month_count = months.size();
  for (size_t i = 0; i < next.month_count; i++) {
    JsonVariantConst m = months[i];
    if (!m["c"].is<uint8_t>() || m["c"].as<uint8_t>() >= next.cols ||
        !m["l"].is<const char*>()) {
      return false;
    }
    const std::string lbl = m["l"].as<const char*>();
    if (lbl.size() > 3) return false;
    next.months[i].col = m["c"].as<uint8_t>();
    std::snprintf(next.months[i].label, sizeof(next.months[i].label), "%s",
                  lbl.c_str());
  }

  JsonArrayConst rows = root["rows"].as<JsonArrayConst>();
  if (rows.size() > kUsageMaxProviders) return false;
  next.row_count = rows.size();
  for (size_t i = 0; i < next.row_count; i++) {
    JsonVariantConst o = rows[i];
    StationRowView& r = next.rows[i];
    if (!small_str(o["id"], r.id, kUsageMaxIdLength) || r.id.empty()) {
      return false;
    }
    int v = 0;
    if (!ranged_int(o["st"], 0, (int)UsageRowState::Stale, v)) return false;
    r.state = (UsageRowState)v;
    if (!ranged_int(o["left"], 0, 100, v)) return false;
    r.left = (uint8_t)v;
    if (!o["low"].is<bool>() || !o["tok"].is<bool>()) return false;
    r.low_fuel = o["low"].as<bool>();
    r.has_token = o["tok"].as<bool>();
    if (!ranged_int(o["car"], -1, kStationBarPx, v)) return false;
    r.car_px = (int16_t)v;
    if (!ranged_int(o["pace"], -1, kStationBarPx, v)) return false;
    r.pace_px = (int16_t)v;
    if (!small_str(o["cd"], r.countdown, 24) ||
        !small_str(o["chip"], r.chip, 24) ||
        !small_str(o["s7"], r.seven_day, 24)) {
      return false;
    }
  }

  out = next;
  return true;
}

PageRecord station_page(const UsageSnapshot& usage, const UsageSync& usage_sync,
                        const TokenSnapshot& token,
                        const MonumentSnapshot& monument,
                        const TokenSync& token_sync, uint64_t now,
                        RuntimeMode runtime_mode, HeatmapWindow window) {
  PageRecord page;
  page.id = kStationPageId;
  page.data_ts = now;
  page.layout_json = encode_station_view(derive_station_view(
      usage, usage_sync, token, monument, token_sync, now, runtime_mode,
      window));
  return page;
}

bool update_station_page(DeviceState& device, const UsageSnapshot& usage,
                         const UsageSync& usage_sync,
                         const TokenSnapshot& token,
                         const MonumentSnapshot& monument,
                         const TokenSync& token_sync, uint64_t now,
                         RuntimeMode runtime_mode, HeatmapWindow window) {
  PageRecord page = station_page(usage, usage_sync, token, monument, token_sync,
                                 now, runtime_mode, window);
  for (PageRecord& existing : device.pages) {
    if (existing.id != kStationPageId) continue;
    if (existing.layout_json == page.layout_json) return false;
    existing = std::move(page);
    return true;
  }
  device.pages.insert(device.pages.begin(), std::move(page));
  return true;
}

}  // namespace rcpaper
