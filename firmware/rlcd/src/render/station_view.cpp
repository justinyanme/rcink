#include "render/station_view.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <memory>

namespace rcpaper {
namespace {

constexpr uint64_t kClockMinimum = 1704067200ULL;
constexpr uint64_t kOldSourceAgeS = 2 * 3600;
constexpr size_t kStationViewMaxBytes = 4096;
constexpr const char* kStationSchema = "rcink.station-view.v2";
constexpr double kPi = 3.14159265358979323846;

std::string local_hhmm(uint64_t epoch_s) {
  const time_t instant = (time_t)epoch_s;
  std::tm local = {};
  localtime_r(&instant, &local);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", local.tm_hour, local.tm_min);
  return buf;
}

uint16_t local_minutes_of_day(uint64_t epoch_s) {
  const time_t instant = (time_t)epoch_s;
  std::tm local = {};
  localtime_r(&instant, &local);
  const int mins = local.tm_hour * 60 + local.tm_min;
  return (uint16_t)((mins % 1440 + 1440) % 1440);
}

uint64_t sat_add(uint64_t a, uint64_t b) {
  if (b > std::numeric_limits<uint64_t>::max() - a) {
    return std::numeric_limits<uint64_t>::max();
  }
  return a + b;
}

uint64_t sum_range(const std::vector<uint64_t>& days, size_t start, size_t end) {
  uint64_t total = 0;
  const size_t clamped_end = std::min(end, days.size());
  for (size_t i = start; i < clamped_end; i++) {
    total = sat_add(total, days[i]);
  }
  return total;
}

// Primary token providers always reserve a numeric 7D slot ("--" before sync,
// "0" when zero); backup providers populate 7D when present in
// TokenSnapshot and fall back to an em-dash when absent.
bool is_always_numeric_token_provider(const std::string& id) {
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

bool valid_skyline_shape(SkylineScale scale, const std::string& label,
                         uint8_t bar_count) {
  switch (scale) {
    case SkylineScale::SevenDays:  return label == "7D" && bar_count == 7;
    case SkylineScale::EightWeeks: return label == "8W" && bar_count == 8;
    case SkylineScale::SixMonths:  return label == "6M" && bar_count == 6;
  }
  return false;
}

}  // namespace

SkyPhase sky_phase_for_minutes(uint16_t minutes_of_day) {
  const uint16_t m = (uint16_t)(minutes_of_day % 1440);
  if (m >= 330 && m < 450) return SkyPhase::Dawn;    // 05:30–07:30
  if (m >= 450 && m < 1020) return SkyPhase::Day;    // 07:30–17:00
  if (m >= 1020 && m < 1170) return SkyPhase::Dusk;  // 17:00–19:30
  return SkyPhase::Night;                            // 19:30–05:30
}

SkyVisualState derive_sky_visual(uint16_t minutes_of_day, bool animating,
                                 uint32_t tick) {
  const uint16_t m = (uint16_t)(minutes_of_day % 1440);
  SkyVisualState sky;
  sky.phase = sky_phase_for_minutes(m);
  sky.beacon_on = !animating || (((tick >> 1) & 1u) == 0);

  if (sky.phase == SkyPhase::Night) {
    sky.is_moon = true;
    const uint16_t night_mins =
        m >= 1170 ? (uint16_t)(m - 1170) : (uint16_t)(m + 270);
    const double nt = std::max(0.0, std::min(1.0, (double)night_mins / 600.0));
    sky.body_x = (int16_t)(kStationSkylineX + 6 +
                           std::lround(nt * (kStationSkylineW - 18)));
    sky.body_y = (int16_t)(kStationSkylineTop + 12 -
                           std::lround(std::sin(nt * kPi) * 11.0));
    sky.twinkle_phase = animating ? (uint8_t)((tick / 3u) % 4u) : 0;
  } else {
    sky.is_moon = false;
    const double dt =
        std::max(0.0, std::min(1.0, (double)((int)m - 330) / 840.0));
    sky.body_x = (int16_t)(kStationSkylineX + 4 +
                           std::lround(dt * (kStationSkylineW - 15)));
    sky.body_y = (int16_t)(kStationSkylineTop + 13 -
                           std::lround(std::sin(dt * kPi) * 12.0));
    if (animating) {
      const int32_t span = kStationSkylineW + 16;
      sky.cloud1_x = (int16_t)(((int32_t)((tick * 45u) / 100u) % span) - 8);
      sky.cloud2_x =
          (int16_t)(((int32_t)((tick * 28u) / 100u + 56u) % span) - 8);
    } else {
      sky.cloud1_x = 18;
      sky.cloud2_x = 72;
    }
  }
  return sky;
}

TokenSkylineView derive_token_skyline(const std::vector<uint64_t>& shifted_days,
                                      bool has_data, SkylineScale scale) {
  TokenSkylineView out;
  out.scale = scale;
  out.label = skyline_scale_label(scale);
  out.has_data = has_data;
  out.bar_heights.fill(0);
  out.above_baseline.fill(0);

  std::array<uint64_t, kSkylineMaxBars> raw{};
  long double baseline = 0.0L;
  const bool have_full_year = shifted_days.size() >= kMonumentDays;

  if (scale == SkylineScale::SevenDays) {
    out.bar_count = 7;
    if (have_full_year) {
      for (size_t i = 0; i < 7; i++) {
        raw[i] = shifted_days[kMonumentDays - 7 + i];
      }
      out.current_sum = sum_range(shifted_days, kMonumentDays - 7, kMonumentDays);
      out.prior_sum =
          sum_range(shifted_days, kMonumentDays - 14, kMonumentDays - 7);
      baseline = (long double)out.prior_sum / 7.0L;
    }
  } else if (scale == SkylineScale::SixMonths) {
    out.bar_count = 6;
    if (have_full_year) {
      for (size_t m = 0; m < 6; m++) {
        const size_t offset = (5 - m) * 30;
        const size_t end = kMonumentDays - offset;
        raw[m] = sum_range(shifted_days, end - 30, end);
      }
      out.current_sum = raw[5];
      out.prior_sum = raw[4];
      long double prior_five = 0.0L;
      for (size_t i = 0; i < 5; i++) prior_five += (long double)raw[i];
      baseline = prior_five / 5.0L;
    }
  } else {
    out.bar_count = 8;
    if (have_full_year) {
      for (size_t w = 0; w < 8; w++) {
        const size_t offset = (7 - w) * 7;
        const size_t end = kMonumentDays - offset;
        raw[w] = sum_range(shifted_days, end - 7, end);
      }
      out.current_sum = raw[7];
      out.prior_sum = raw[6];
      long double prior_seven = 0.0L;
      for (size_t i = 0; i < 7; i++) prior_seven += (long double)raw[i];
      baseline = prior_seven / 7.0L;
    }
  }

  if (!has_data) {
    out.has_delta = false;
    out.delta_pct = 0;
    out.delta_positive = false;
    out.delta_label = "--";
  } else if (out.prior_sum == 0 && out.current_sum == 0) {
    out.has_delta = false;
    out.delta_pct = 0;
    out.delta_positive = false;
    out.delta_label = "0%";
  } else if (out.prior_sum == 0) {
    out.has_delta = true;
    out.delta_pct = 999;
    out.delta_positive = true;
    out.delta_label = "NEW";
  } else {
    const long double diff =
        (long double)out.current_sum - (long double)out.prior_sum;
    const long pct_raw = std::lround((diff * 100.0L) / (long double)out.prior_sum);
    const long pct = std::max<long>(-100, std::min<long>(999, pct_raw));
    out.has_delta = true;
    out.delta_pct = (int16_t)pct;
    out.delta_positive = pct >= 0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%s%ld%%", pct >= 0 ? "+" : "", pct);
    out.delta_label = buf;
  }

  long double bar_max = baseline * 1.1L;
  for (size_t i = 0; i < out.bar_count; i++) {
    bar_max = std::max(bar_max, (long double)raw[i]);
  }
  if (bar_max > 0.0L) {
    out.baseline_px = (uint8_t)std::min<long>(
        kStationSkylineMaxBarH,
        std::max<long>(
            0, std::lround((baseline / bar_max) * kStationSkylineMaxBarH)));
    for (size_t i = 0; i < out.bar_count; i++) {
      const long h = std::lround(((long double)raw[i] / bar_max) *
                                 kStationSkylineMaxBarH);
      out.bar_heights[i] =
          (uint8_t)std::min<long>(kStationSkylineMaxBarH, std::max<long>(5, h));
      out.above_baseline[i] =
          ((long double)raw[i] >= baseline && raw[i] > 0) ? 1 : 0;
    }
  } else if (has_data) {
    out.bar_heights.fill(5);
    out.baseline_px = 0;
  }

  return out;
}

StationView derive_station_view(const UsageSnapshot& usage,
                                const UsageSync& usage_sync,
                                const TokenSnapshot& token,
                                const MonumentSnapshot& monument,
                                const TokenSync& token_sync, uint64_t now,
                                RuntimeMode runtime_mode,
                                HeatmapWindow window) {
  auto mv = std::make_unique<MonumentView>(
      derive_monument_view(monument, token, now, token_sync, runtime_mode,
                           window));
  auto uv = std::make_unique<UsageView>(
      derive_usage_view(usage, now, usage_sync));
  auto tv = std::make_unique<TokenView>(
      derive_token_view(token, now, token_sync));

  StationView view;
  view.header_left = "STATION";
  view.eco_last_sync = (runtime_mode == RuntimeMode::Eco);

  const bool clock_valid = now >= kClockMinimum;
  const uint64_t newest_sync_ok =
      std::max(usage_sync.last_ok_s, token_sync.last_ok_s);
  if (clock_valid) {
    if (view.eco_last_sync) {
      if (newest_sync_ok >= kClockMinimum) {
        view.clock_hhmm = local_hhmm(newest_sync_ok);
        view.minutes_of_day = local_minutes_of_day(newest_sync_ok);
      } else {
        view.clock_hhmm = "--:--";
        view.minutes_of_day = 750;
      }
    } else {
      view.clock_hhmm = local_hhmm(now);
      view.minutes_of_day = local_minutes_of_day(now);
    }
  } else {
    view.clock_hhmm = "--:--";
    view.minutes_of_day = 750;
  }

  view.total_odo_value = mv->total_odo_value;
  view.total_odo = mv->total_odo;

  bool has_token_history = false;
  const std::vector<uint64_t> shifted_days =
      derive_monument_shifted_days(monument, token, now, has_token_history);
  uint64_t today_tokens = shifted_days[kMonumentDays - 1];
  uint32_t streak_days = 0;
  for (int i = (int)kMonumentDays - 2; i >= 0; i--) {
    if (shifted_days[(size_t)i] > 0) streak_days++;
    else break;
  }
  if (today_tokens > 0) streak_days++;

  view.today_tokens = today_tokens;
  view.today_total = has_token_history ? format_tokens(today_tokens) : "--";
  char streak_buf[16];
  std::snprintf(streak_buf, sizeof(streak_buf), "%ud", (unsigned)streak_days);
  view.streak_label = streak_buf;

  view.window_label = mv->window_label;
  view.cols = mv->cols;
  view.cells = mv->cells;
  view.months = mv->months;
  view.month_count = mv->month_count;

  view.skyline = derive_token_skyline(shifted_days, has_token_history,
                                      skyline_scale_for_window(window));

  view.row_count = uv->count;
  for (size_t i = 0; i < uv->count; i++) {
    const UsageRowView& src = uv->rows[i];
    StationRowView& dst = view.rows[i];
    dst.id = src.id;
    dst.state = src.state;
    dst.left = src.left;
    dst.low_fuel = station_low_fuel(src);
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

    int token_idx = -1;
    for (size_t p = 0; p < tv->count; p++) {
      if (tv->rows[p].id == src.id) {
        token_idx = (int)p;
        break;
      }
    }
    if (token_idx >= 0) {
      dst.has_token = true;
      dst.seven_day = tv->rows[(size_t)token_idx].seven_day;
    } else if (is_always_numeric_token_provider(src.id)) {
      dst.has_token = true;
      dst.seven_day = token.today.empty() ? "--" : "0";
    } else {
      dst.has_token = false;
      dst.seven_day.clear();
    }
  }
  derive_station_footer(*uv, usage_sync, token, token_sync, now,
                        view.footer_chip, view.footer_text);
  return view;
}

void derive_station_footer(const UsageView& allowance, const UsageSync& usage_sync,
                           const TokenSnapshot& token, const TokenSync& token_sync,
                           uint64_t now, std::string& chip, std::string& text) {
  const bool clock_valid = now >= kClockMinimum;
  chip.clear();
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
    chip = "SYNCING";
    text = "fetching now";
  } else if (link == LinkFailure::WifiFailed) {
    chip = "NO WIFI";
    text =
        retry_footer_text(now, clock_valid, link_retry_at, link_cooldown);
  } else if (link == LinkFailure::NtpFailed) {
    chip = "NO NTP";
    text =
        retry_footer_text(now, clock_valid, link_retry_at, link_cooldown);
  } else if (!clock_valid) {
    chip = "NO CLOCK";
    text = "waiting for NTP";
  } else if (usage_sync.fetch_failed && token_sync.fetch_failed) {
    const uint64_t retry_at =
        (usage_sync.retry_at_s > 0 && token_sync.retry_at_s > 0)
            ? std::min(usage_sync.retry_at_s, token_sync.retry_at_s)
            : std::max(usage_sync.retry_at_s, token_sync.retry_at_s);
    const uint32_t cooldown =
        std::min(usage_sync.retry_cooldown_s, token_sync.retry_cooldown_s);
    chip = "SYNC FAILED";
    text = retry_footer_text(now, clock_valid, retry_at, cooldown);
  } else if (usage_sync.fetch_failed) {
    chip = "ALLOWANCE";
    text = retry_footer_text(
        now, clock_valid, usage_sync.retry_at_s, usage_sync.retry_cooldown_s);
  } else if (token_sync.fetch_failed) {
    chip = "TOKEN FAIL";
    text = retry_footer_text(
        now, clock_valid, token_sync.retry_at_s, token_sync.retry_cooldown_s);
  } else if (token_old_data) {
    chip = "OLD DATA";
    text = token.required_source_collected_at == 0
                           ? "M4 --:--"
                           : "M4 " + local_hhmm(token.required_source_collected_at);
  } else if (allowance.footer_chip == "OLD DATA") {
    chip = "OLD DATA";
    text = usage_sync.last_ok_s
                           ? "checked " + local_hhmm(usage_sync.last_ok_s)
                           : "not synced yet";
  } else if (usage_sync.last_ok_s == 0 && token_sync.last_ok_s == 0) {
    text = "Not synced yet";
  } else {
    const uint64_t conservative_ok =
        (usage_sync.last_ok_s > 0 && token_sync.last_ok_s > 0)
            ? std::min(usage_sync.last_ok_s, token_sync.last_ok_s)
            : std::max(usage_sync.last_ok_s, token_sync.last_ok_s);
    text = "Synced " + local_hhmm(conservative_ok);
  }
}

std::string encode_station_view(const StationView& view) {
  JsonDocument doc;
  doc["schema"] = kStationSchema;
  doc["win"] = view.window_label;
  doc["cols"] = view.cols;
  doc["headerLeft"] = view.header_left;
  doc["ecoSync"] = view.eco_last_sync;
  doc["clock"] = view.clock_hhmm;
  doc["mins"] = view.minutes_of_day;
  doc["odoVal"] = view.total_odo_value;
  doc["odo"] = view.total_odo;
  doc["todayVal"] = view.today_tokens;
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

  JsonObject sky = doc["sky"].to<JsonObject>();
  sky["sc"] = (int)view.skyline.scale;
  sky["lbl"] = view.skyline.label;
  sky["base"] = (int)view.skyline.baseline_px;
  sky["dPct"] = (int)view.skyline.delta_pct;
  sky["dHas"] = view.skyline.has_delta;
  sky["dPos"] = view.skyline.delta_positive;
  sky["dLbl"] = view.skyline.delta_label;
  sky["ok"] = view.skyline.has_data;
  JsonArray bars = sky["bars"].to<JsonArray>();
  JsonArray hi = sky["hi"].to<JsonArray>();
  for (size_t i = 0; i < view.skyline.bar_count && i < kSkylineMaxBars; i++) {
    bars.add((int)view.skyline.bar_heights[i]);
    hi.add((int)view.skyline.above_baseline[i]);
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
      !root["todayVal"].is<uint64_t>() || !root["cells"].is<const char*>() ||
      !root["months"].is<JsonArrayConst>() ||
      !root["sky"].is<JsonObjectConst>() ||
      !root["rows"].is<JsonArrayConst>()) {
    return false;
  }

  StationView next;
  next.cells.fill(kMonumentCellOutside);
  next.cols = root["cols"].as<uint8_t>();
  next.eco_last_sync = root["ecoSync"].as<bool>();
  int mins_i = 0;
  if (!ranged_int(root["mins"], 0, 1439, mins_i)) return false;
  next.minutes_of_day = (uint16_t)mins_i;
  next.total_odo_value = root["odoVal"].as<uint64_t>();
  next.today_tokens = root["todayVal"].as<uint64_t>();
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

  JsonObjectConst sky = root["sky"].as<JsonObjectConst>();
  int v = 0;
  if (!ranged_int(sky["sc"], 0, (int)SkylineScale::SixMonths, v)) return false;
  next.skyline.scale = (SkylineScale)v;
  if (!small_str(sky["lbl"], next.skyline.label, 8) ||
      !small_str(sky["dLbl"], next.skyline.delta_label, 12) ||
      !ranged_int(sky["base"], 0, kStationSkylineMaxBarH, v)) {
    return false;
  }
  next.skyline.baseline_px = (uint8_t)v;
  if (!ranged_int(sky["dPct"], -100, 999, v)) return false;
  next.skyline.delta_pct = (int16_t)v;
  if (!sky["dHas"].is<bool>() || !sky["dPos"].is<bool>() ||
      !sky["ok"].is<bool>() || !sky["bars"].is<JsonArrayConst>() ||
      !sky["hi"].is<JsonArrayConst>()) {
    return false;
  }
  next.skyline.has_delta = sky["dHas"].as<bool>();
  next.skyline.delta_positive = sky["dPos"].as<bool>();
  next.skyline.has_data = sky["ok"].as<bool>();
  JsonArrayConst bars = sky["bars"].as<JsonArrayConst>();
  JsonArrayConst hi = sky["hi"].as<JsonArrayConst>();
  if (bars.size() > kSkylineMaxBars || bars.size() != hi.size()) return false;
  next.skyline.bar_count = (uint8_t)bars.size();
  if (!valid_skyline_shape(next.skyline.scale, next.skyline.label,
                           next.skyline.bar_count)) {
    return false;
  }
  for (size_t i = 0; i < next.skyline.bar_count; i++) {
    if (!ranged_int(bars[i], 0, kStationSkylineMaxBarH, v)) return false;
    next.skyline.bar_heights[i] = (uint8_t)v;
    if (!ranged_int(hi[i], 0, 1, v)) return false;
    next.skyline.above_baseline[i] = (uint8_t)v;
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
  auto view = std::make_unique<StationView>(derive_station_view(
      usage, usage_sync, token, monument, token_sync, now, runtime_mode,
      window));
  page.layout_json = encode_station_view(*view);
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
