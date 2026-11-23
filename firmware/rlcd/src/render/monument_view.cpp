#include "render/monument_view.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <vector>

#include "render/token_view.h"

namespace rcpaper {
namespace {

constexpr uint64_t kClockMinimum = 1704067200ULL;
constexpr uint64_t kOldSourceAgeS = 2 * 3600;
constexpr size_t kMonumentViewMaxBytes = 4096;

int parse_ymd(const std::string& value, int& year, int& month, int& day) {
  if (value.size() != 10) return 0;
  year = (value[0] - '0') * 1000 + (value[1] - '0') * 100 +
         (value[2] - '0') * 10 + value[3] - '0';
  month = (value[5] - '0') * 10 + (value[6] - '0');
  day = (value[8] - '0') * 10 + (value[9] - '0');
  return 1;
}

int64_t days_from_civil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = (unsigned)(year - era * 400);
  const unsigned doy =
      (153 * (month + (month > 2 ? (unsigned)-3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

void civil_from_days(int64_t z, int& year, unsigned& month, unsigned& day) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  year = (int)yoe + (int)era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp + (mp < 10 ? 3 : (unsigned)-9);
  year += (month <= 2);
}

int monday_weekday(int64_t cday) {
  // 1970-01-01 was Thursday (index 3 in Mon=0..Sun=6).
  const int mod = (int)((cday % 7 + 7) % 7);
  return (mod + 3) % 7;
}

int64_t elapsed_days_utc8(const std::string& server_end_date, uint64_t now) {
  if (now < kClockMinimum || server_end_date.empty()) return 0;
  int y = 0, m = 0, d = 0;
  if (!parse_ymd(server_end_date, y, m, d)) return 0;
  const int64_t server = days_from_civil(y, (unsigned)m, (unsigned)d);
  const int64_t device = (int64_t)(now + 8 * 3600) / 86400;
  return device > server ? device - server : 0;
}

std::string local_hhmm(uint64_t epoch_s) {
  const time_t instant = (time_t)epoch_s;
  std::tm local = {};
  localtime_r(&instant, &local);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", local.tm_hour, local.tm_min);
  return buf;
}

std::string format_header_date(uint64_t epoch_s) {
  static const char* const kW[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char* const kM[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                   "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  const time_t instant = (time_t)epoch_s;
  std::tm local = {};
  localtime_r(&instant, &local);
  char week[8];
  if (std::strftime(week, sizeof(week), "W%V", &local) == 0) {
    std::snprintf(week, sizeof(week), "W--");
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s %d \xC2\xB7 %s",
                kW[local.tm_wday % 7], kM[local.tm_mon % 12],
                local.tm_mday, week);
  return buf;
}

uint64_t sat_add(uint64_t a, uint64_t b) {
  if (b > std::numeric_limits<uint64_t>::max() - a) {
    return std::numeric_limits<uint64_t>::max();
  }
  return a + b;
}

uint64_t sum_token_today(const TokenSnapshot& token, uint64_t now) {
  if (token.count == 0 || token.today.empty()) return 0;
  const int64_t elapsed = elapsed_days_utc8(token.today, now);
  if (elapsed >= (int64_t)kTokenDays) return 0;
  if (elapsed > 0) return 0;  // Token snapshot not yet updated after midnight.
  uint64_t sum = 0;
  for (size_t p = 0; p < token.count; p++) {
    sum = sat_add(sum, token.providers[p].days[kTokenDays - 1]);
  }
  return sum;
}

bool small_str(JsonVariantConst v, std::string& out, size_t max_len = 64) {
  if (!v.is<const char*>()) return false;
  out = v.as<const char*>();
  return out.size() <= max_len;
}

}  // namespace

MonumentView derive_monument_view(const MonumentSnapshot& monument,
                                  const TokenSnapshot& token, uint64_t now,
                                  const TokenSync& sync,
                                  RuntimeMode runtime_mode) {
  MonumentView view;
  view.cells.fill(kMonumentCellOutside);
  const bool clock_valid = now >= kClockMinimum;

  if (clock_valid) {
    view.header_left = format_header_date(now);
    if (runtime_mode == RuntimeMode::Eco) {
      view.clock_caption = "LAST SYNC";
      view.clock_hhmm = sync.last_ok_s >= kClockMinimum
                            ? local_hhmm(sync.last_ok_s)
                            : "--:--";
    } else {
      view.clock_caption = "DESK TIME";
      view.clock_hhmm = local_hhmm(now);
    }
  } else {
    view.header_left = "CLOCK NOT SET";
    view.clock_caption = runtime_mode == RuntimeMode::Eco ? "LAST SYNC" : "DESK TIME";
    view.clock_hhmm = "--:--";
  }

  std::vector<uint64_t> shifted(kMonumentDays, 0);
  int64_t start_c = 0;
  bool has_monument =
      !monument.end_date.empty() && monument.days.size() == kMonumentDays;
  if (has_monument) {
    int y = 0, m = 0, d = 0;
    if (!parse_ymd(monument.end_date, y, m, d)) {
      has_monument = false;
    } else {
      int64_t end_c = days_from_civil(y, (unsigned)m, (unsigned)d);
      const int64_t elapsed = elapsed_days_utc8(monument.end_date, now);
      if (elapsed <= 0) {
        shifted = monument.days;
      } else if (elapsed >= (int64_t)kMonumentDays) {
        std::fill(shifted.begin(), shifted.end(), 0);
        end_c += elapsed;
      } else {
        const size_t shift = (size_t)elapsed;
        for (size_t i = 0; i < kMonumentDays; i++) {
          shifted[i] =
              i + shift < kMonumentDays ? monument.days[i + shift] : 0;
        }
        end_c += elapsed;
      }
      start_c = end_c - (int64_t)(kMonumentDays - 1);
    }
  } else if (clock_valid) {
    const int64_t device_c = (int64_t)(now + 8 * 3600) / 86400;
    start_c = device_c - (int64_t)(kMonumentDays - 1);
  }

  uint64_t year_sum = 0;
  uint64_t peak_day = 0;
  uint32_t active_days = 0;
  std::vector<uint64_t> nonzero;
  nonzero.reserve(kMonumentDays);
  for (size_t i = 0; i < kMonumentDays; i++) {
    uint64_t v = shifted[i];
    if (i == kMonumentDays - 1) {
      const uint64_t tok_today = sum_token_today(token, now);
      if (tok_today > v) v = tok_today;
      shifted[i] = v;
    }
    year_sum = sat_add(year_sum, v);
    if (v > peak_day) peak_day = v;
    if (v > 0) {
      active_days++;
      nonzero.push_back(v);
    }
  }

  uint64_t token_lifetime_sum = 0;
  for (size_t p = 0; p < token.count; p++) {
    token_lifetime_sum = sat_add(token_lifetime_sum, token.providers[p].lifetime);
  }
  view.total_odo_value = std::max(token_lifetime_sum, year_sum);
  view.total_odo = format_tokens(view.total_odo_value);

  char badge_buf[24];
  std::snprintf(badge_buf, sizeof(badge_buf), "%u DAYS", (unsigned)active_days);
  view.active_badge = badge_buf;

  uint32_t streak = 0;
  for (int i = (int)kMonumentDays - 2; i >= 0; i--) {
    if (shifted[(size_t)i] > 0) streak++;
    else break;
  }
  if (shifted[kMonumentDays - 1] > 0) streak++;

  if (!has_monument && token_lifetime_sum == 0) {
    view.summary_left = "Waiting for annual history";
    view.summary_right.clear();
  } else {
    const std::string today_str = format_tokens(shifted[kMonumentDays - 1]);
    char sum_left[64];
    std::snprintf(sum_left, sizeof(sum_left),
                  "today %s \xC2\xB7 %ud streak", today_str.c_str(),
                  (unsigned)streak);
    view.summary_left = sum_left;
    view.summary_right = "peak " + format_tokens(peak_day);
  }

  // Next Odometer milestone target.
  static constexpr uint64_t kMilestones[] = {
      10000000000ULL,   25000000000ULL,   50000000000ULL,
      100000000000ULL,  250000000000ULL,  500000000000ULL,
      1000000000000ULL, 2000000000000ULL, 5000000000000ULL,
  };
  uint64_t target = kMilestones[sizeof(kMilestones) / sizeof(kMilestones[0]) - 1];
  for (uint64_t m : kMilestones) {
    if (view.total_odo_value < m) {
      target = m;
      break;
    }
  }
  view.milestone_label = "NEXT " + format_tokens(target);
  if (view.total_odo_value >= target) {
    view.milestone_pct = 100;
  } else {
    view.milestone_pct = (uint8_t)std::min<long>(
        99, std::lround((long double)view.total_odo_value * 100.0L / target));
  }

  // Quantize nonzero days into 1..4 intensity quartiles.
  uint64_t q1 = 0, q2 = 0, q3 = 0;
  if (!nonzero.empty()) {
    std::sort(nonzero.begin(), nonzero.end());
    const size_t n = nonzero.size();
    q1 = nonzero[n / 4];
    q2 = nonzero[n / 2];
    q3 = nonzero[(3 * n) / 4];
  }

  if (start_c != 0) {
    static const char* const kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                          "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    const int start_dow = monday_weekday(start_c);
    for (size_t i = 0; i < kMonumentDays; i++) {
      const size_t pos = (size_t)start_dow + i;
      const size_t col = pos / kMonumentRows;
      const size_t row = pos % kMonumentRows;
      if (col < kMonumentCols) {
        const uint64_t v = shifted[i];
        uint8_t lvl = 0;
        if (v > 0) {
          if (v >= q3) lvl = 4;
          else if (v >= q2) lvl = 3;
          else if (v >= q1) lvl = 2;
          else lvl = 1;
        }
        view.cells[col * kMonumentRows + row] = lvl;
      }
      int y = 0;
      unsigned m = 0, d = 0;
      civil_from_days(start_c + (int64_t)i, y, m, d);
      if (d == 1 && col < kMonumentCols && view.month_count < 12) {
        MonumentMonthTick& tick = view.months[view.month_count++];
        tick.col = (uint8_t)col;
        std::snprintf(tick.label, sizeof(tick.label), "%s", kMonths[(m - 1) % 12]);
      }
    }
  } else {
    // Still render empty matrix grid before clock/data arrive.
    for (size_t col = 0; col < kMonumentCols - 1; col++) {
      for (size_t row = 0; row < kMonumentRows; row++) {
        view.cells[col * kMonumentRows + row] = 0;
      }
    }
  }

  const bool old_data =
      !token.today.empty() &&
      (token.required_source_collected_at == 0 ||
       (clock_valid && now > token.required_source_collected_at &&
        now - token.required_source_collected_at > kOldSourceAgeS));

  if (sync.syncing) {
    view.footer_chip = "SYNCING";
    view.footer_text = "fetching now";
  } else if (sync.link_failure == LinkFailure::WifiFailed) {
    view.footer_chip = "NO WIFI";
    view.footer_text = retry_footer_text(now, clock_valid, sync.retry_at_s,
                                         sync.retry_cooldown_s);
  } else if (sync.link_failure == LinkFailure::NtpFailed) {
    view.footer_chip = "NO NTP";
    view.footer_text = retry_footer_text(now, clock_valid, sync.retry_at_s,
                                         sync.retry_cooldown_s);
  } else if (!clock_valid) {
    view.footer_chip = "NO CLOCK";
    view.footer_text = "waiting for NTP";
  } else if (sync.fetch_failed) {
    view.footer_chip = "SYNC FAILED";
    view.footer_text = retry_footer_text(now, clock_valid, sync.retry_at_s,
                                         sync.retry_cooldown_s);
  } else if (old_data) {
    view.footer_chip = "OLD DATA";
    view.footer_text = token.required_source_collected_at == 0
        ? "M4 --:--"
        : "M4 " + local_hhmm(token.required_source_collected_at);
  } else if (sync.last_ok_s == 0) {
    view.footer_text = "Not synced yet";
  } else {
    view.footer_text = "Synced " + local_hhmm(sync.last_ok_s);
  }
  return view;
}

std::string encode_monument_view(const MonumentView& view) {
  JsonDocument doc;
  doc["schema"] = "rcink.monument-view.v1";
  doc["headerLeft"] = view.header_left;
  doc["clockCap"] = view.clock_caption;
  doc["clock"] = view.clock_hhmm;
  doc["odoVal"] = view.total_odo_value;
  doc["odo"] = view.total_odo;
  doc["badge"] = view.active_badge;
  doc["sumL"] = view.summary_left;
  doc["sumR"] = view.summary_right;
  doc["msLabel"] = view.milestone_label;
  doc["msPct"] = view.milestone_pct;
  doc["fChip"] = view.footer_chip;
  doc["fText"] = view.footer_text;
  std::string cell_str;
  cell_str.reserve(view.cells.size());
  for (uint8_t c : view.cells) {
    cell_str.push_back(c == kMonumentCellOutside ? '.' : (char)('0' + (c & 7)));
  }
  doc["cells"] = cell_str;
  JsonArray months = doc["months"].to<JsonArray>();
  for (size_t i = 0; i < view.month_count; i++) {
    JsonObject m = months.add<JsonObject>();
    m["c"] = view.months[i].col;
    m["l"] = view.months[i].label;
  }
  std::string json;
  serializeJson(doc, json);
  return json;
}

bool decode_monument_view(const std::string& json, MonumentView& out) {
  if (json.empty() || json.size() > kMonumentViewMaxBytes) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  JsonVariantConst root = doc.as<JsonVariantConst>();
  if (root["schema"] != "rcink.monument-view.v1" ||
      !root["odoVal"].is<uint64_t>() || !root["msPct"].is<uint8_t>() ||
      !root["cells"].is<const char*>() ||
      !root["months"].is<JsonArrayConst>()) {
    return false;
  }
  MonumentView next;
  next.total_odo_value = root["odoVal"].as<uint64_t>();
  const int pct = root["msPct"].as<int>();
  if (pct < 0 || pct > 100) return false;
  next.milestone_pct = (uint8_t)pct;
  if (!small_str(root["headerLeft"], next.header_left) ||
      !small_str(root["clockCap"], next.clock_caption, 24) ||
      !small_str(root["clock"], next.clock_hhmm, 12) ||
      !small_str(root["odo"], next.total_odo, 16) ||
      !small_str(root["badge"], next.active_badge, 24) ||
      !small_str(root["sumL"], next.summary_left) ||
      !small_str(root["sumR"], next.summary_right) ||
      !small_str(root["msLabel"], next.milestone_label, 24) ||
      !small_str(root["fChip"], next.footer_chip, 24) ||
      !small_str(root["fText"], next.footer_text)) {
    return false;
  }
  const std::string cell_str = root["cells"].as<const char*>();
  if (cell_str.size() != next.cells.size()) return false;
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
    if (!m["c"].is<uint8_t>() || m["c"].as<uint8_t>() >= kMonumentCols ||
        !m["l"].is<const char*>()) {
      return false;
    }
    const std::string lbl = m["l"].as<const char*>();
    if (lbl.size() > 3) return false;
    next.months[i].col = m["c"].as<uint8_t>();
    std::snprintf(next.months[i].label, sizeof(next.months[i].label), "%s",
                  lbl.c_str());
  }
  out = next;
  return true;
}

PageRecord monument_page(const MonumentSnapshot& monument,
                         const TokenSnapshot& token, uint64_t now,
                         const TokenSync& sync, RuntimeMode runtime_mode) {
  PageRecord page;
  page.id = kMonumentPageId;
  page.data_ts = now;
  page.layout_json = encode_monument_view(
      derive_monument_view(monument, token, now, sync, runtime_mode));
  return page;
}

bool update_monument_page(DeviceState& device, const MonumentSnapshot& monument,
                          const TokenSnapshot& token, uint64_t now,
                          const TokenSync& sync, RuntimeMode runtime_mode) {
  PageRecord page = monument_page(monument, token, now, sync, runtime_mode);
  for (PageRecord& existing : device.pages) {
    if (existing.id != kMonumentPageId) continue;
    if (existing.layout_json == page.layout_json) return false;
    existing = std::move(page);
    return true;
  }
  device.pages.push_back(std::move(page));
  return true;
}

}  // namespace rcpaper
