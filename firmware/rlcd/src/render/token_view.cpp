#include "render/token_view.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>

namespace rcpaper {

const char* const kTokenProviderNames[kTokenProviderCount] = {"Claude", "Codex", "Gemini"};

namespace {
constexpr uint64_t kClockMinimum = 1704067200ULL;
constexpr uint64_t kOldSourceAgeS = 2 * 3600;
constexpr size_t kTokenViewMaximumBytes = 6144;

std::string local_hhmm(uint64_t epoch_s) {
  const time_t instant = (time_t)epoch_s;
  std::tm local = {};
  localtime_r(&instant, &local);
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d", local.tm_hour, local.tm_min);
  return buffer;
}

uint64_t saturated_sum(const std::array<uint64_t, kTokenDays>& days, size_t first) {
  uint64_t total = 0;
  for (size_t i = first; i < kTokenDays; i++) {
    if (days[i] > std::numeric_limits<uint64_t>::max() - total) {
      return std::numeric_limits<uint64_t>::max();
    }
    total += days[i];
  }
  return total;
}

long double complete_day_sum(const std::array<uint64_t, kTokenDays>& days) {
  long double total = 0;
  for (size_t i = 22; i <= 28; i++) total += days[i];
  return total;
}

int parse_date(const std::string& value, int& year, int& month, int& day) {
  if (value.size() != 10) return 0;
  year = (value[0] - '0') * 1000 + (value[1] - '0') * 100 +
         (value[2] - '0') * 10 + value[3] - '0';
  month = (value[5] - '0') * 10 + value[6] - '0';
  day = (value[8] - '0') * 10 + value[9] - '0';
  return 1;
}

// Days since 1970-01-01, using calendar arithmetic rather than mktime so the
// shift is independent of DST and of the persisted servedAt clock precision.
int64_t days_from_civil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = (unsigned)(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? (unsigned)-3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

int64_t elapsed_local_days(const std::string& server_today, uint64_t now) {
  if (now < kClockMinimum) return 0;
  int year, month, day;
  if (!parse_date(server_today, year, month, day)) return 0;
  const int64_t server = days_from_civil(year, (unsigned)month, (unsigned)day);
  // The Worker defines today in Asia/Shanghai, independent of device chrome TZ.
  const int64_t device = (int64_t)(now + 8 * 3600) / 86400;
  return device > server ? device - server : 0;
}

void shift_days(const TokenProvider& provider, int64_t elapsed, TokenRowView& row) {
  if (elapsed <= 0) {
    row.shifted_days = provider.days;
  } else if (elapsed >= (int64_t)kTokenDays) {
    row.shifted_days.fill(0);
  } else {
    const size_t count = (size_t)elapsed;
    for (size_t i = 0; i < kTokenDays; i++) {
      row.shifted_days[i] = i + count < kTokenDays ? provider.days[i + count] : 0;
    }
  }
  row.lifetime_value = provider.lifetime;
  row.lifetime = format_tokens(provider.lifetime);
  row.seven_day = format_tokens(saturated_sum(row.shifted_days, 23));

  uint64_t maximum = 0;
  for (uint64_t value : row.shifted_days) maximum = std::max(maximum, value);
  const long double usual = complete_day_sum(row.shifted_days) / 7.0L;
  for (size_t i = 0; i < kTokenDays; i++) {
    if (maximum == 0 || row.shifted_days[i] == 0) row.bars[i] = 0;
    else row.bars[i] = (uint8_t)std::max<long>(1, std::lround(
        (long double)row.shifted_days[i] * kTokenChartPx / maximum));
  }
  // A 1-2 px drawRect has no interior; keep today's hollow bar distinguishable.
  row.bars[29] = std::max<uint8_t>(3, row.bars[29]);
  row.usual_px = usual > 0 && maximum > 0
      ? (uint8_t)std::max<long>(1, std::lround(usual * kTokenChartPx / maximum)) : 0;

  const std::string today = format_tokens(row.shifted_days[29]);
  if (usual == 0) {
    row.today = "today " + today;
  } else {
    const long double percent = (long double)row.shifted_days[29] * 100.0L / usual;
    char percent_label[40];
    if (percent > 999) std::snprintf(percent_label, sizeof(percent_label), "999%%+");
    else std::snprintf(percent_label, sizeof(percent_label), "%.0Lf%%", percent);
    char label[160];
    std::snprintf(label, sizeof(label), "today %s · %s of a usual day",
                  today.c_str(), percent_label);
    row.today = label;
  }
}

bool small_string(JsonVariantConst value, std::string& out) {
  if (!value.is<const char*>()) return false;
  out = value.as<const char*>();
  return out.size() <= 180;
}
}  // namespace

std::string format_tokens(uint64_t value) {
  if (value == 0) return "0";
  static const uint64_t divisors[] = {1ULL, 1000ULL, 1000000ULL, 1000000000ULL,
                                      1000000000000ULL, 1000000000000000ULL,
                                      1000000000000000000ULL};
  static const char* const suffixes[] = {"", "K", "M", "B", "T", "Qa", "Qi"};
  size_t unit = 0;
  while (unit + 1 < sizeof(divisors) / sizeof(divisors[0]) && value >= divisors[unit + 1]) unit++;

  long double scaled = (long double)value / divisors[unit];
  int decimals = scaled < 10 ? 2 : scaled < 100 ? 1 : 0;
  const long double factor = decimals == 2 ? 100.0L : decimals == 1 ? 10.0L : 1.0L;
  long double rounded = std::round(scaled * factor) / factor;
  if (rounded >= 1000 && unit + 1 < sizeof(divisors) / sizeof(divisors[0])) {
    unit++;
    scaled = (long double)value / divisors[unit];
    decimals = scaled < 10 ? 2 : scaled < 100 ? 1 : 0;
  }
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%.*Lf", decimals, scaled);
  std::string number(buffer);
  if (number.find('.') != std::string::npos) {
    while (!number.empty() && number.back() == '0') number.pop_back();
    if (!number.empty() && number.back() == '.') number.pop_back();
  }
  return number + suffixes[unit];
}

TokenView derive_token_view(const TokenSnapshot& snapshot, uint64_t now,
                            const TokenSync& sync) {
  TokenView view;
  const int64_t elapsed = elapsed_local_days(snapshot.today, now);
  const bool clock_valid = now >= kClockMinimum;
  const bool has_snapshot = !snapshot.today.empty();
  const bool old_data = has_snapshot &&
      (snapshot.required_source_collected_at == 0 ||
       (now >= kClockMinimum && now > snapshot.required_source_collected_at &&
        now - snapshot.required_source_collected_at > kOldSourceAgeS));
  long double combined_usual = 0;
  uint64_t busiest_total = 0;
  int busiest = -1;
  for (size_t p = 0; p < kTokenProviderCount; p++) {
    shift_days(snapshot.providers[p], elapsed, view.rows[p]);
    combined_usual += complete_day_sum(view.rows[p].shifted_days) / 7.0L;
    const uint64_t seven = saturated_sum(view.rows[p].shifted_days, 23);
    if (seven > busiest_total) {
      busiest_total = seven;
      busiest = (int)p;
    }
  }

  int tear = -1;
  if (clock_valid && !old_data && !sync.fetch_failed) {
    for (size_t p = 0; p < kTokenProviderCount; p++) {
      const auto& days = view.rows[p].shifted_days;
      const long double usual = complete_day_sum(days) / 7.0L;
      const long double today = days[29];
      if (usual > 0 && today >= usual * 1.5L && today >= combined_usual * 0.25L) {
        tear = (int)p;
        break;
      }
    }
  }
  if (tear >= 0) {
    view.headline = std::string(kTokenProviderNames[tear]) + " is on a tear today";
    view.headline_pick = (int8_t)tear;
  } else if (busiest >= 0) {
    view.headline = std::string("Busiest this week: ") + kTokenProviderNames[busiest];
    view.headline_pick = (int8_t)busiest;
  } else {
    view.headline = "Waiting for the first numbers.";
  }

  if (sync.syncing) {
    view.footer_chip = "SYNCING";
    view.footer_text = "fetching now";
  } else if (!clock_valid) {
    view.footer_chip = "NO CLOCK";
    view.footer_text = "waiting for NTP";
  } else if (sync.fetch_failed) {
    view.footer_chip = "SYNC FAILED";
    view.footer_text = sync.retry_at_s > now ? "retry " + local_hhmm(sync.retry_at_s) : "retrying";
  } else if (old_data) {
    view.footer_chip = "OLD DATA";
    view.footer_text = snapshot.required_source_collected_at == 0
        ? "M4 --:--" : "M4 " + local_hhmm(snapshot.required_source_collected_at);
  } else if (sync.last_ok_s == 0) {
    view.footer_text = "Not synced yet";
  } else {
    view.footer_text = "Synced " + local_hhmm(sync.last_ok_s);
  }
  return view;
}

std::string encode_token_view(const TokenView& view) {
  JsonDocument doc;
  doc["schema"] = "rcink.token-view.v1";
  doc["headline"] = view.headline;
  doc["pick"] = (int)view.headline_pick;
  doc["footerChip"] = view.footer_chip;
  doc["footer"] = view.footer_text;
  JsonArray rows = doc["rows"].to<JsonArray>();
  for (const TokenRowView& row : view.rows) {
    JsonObject out = rows.add<JsonObject>();
    JsonArray days = out["days"].to<JsonArray>();
    JsonArray bars = out["bars"].to<JsonArray>();
    for (size_t i = 0; i < kTokenDays; i++) {
      days.add(row.shifted_days[i]);
      bars.add(row.bars[i]);
    }
    out["usual"] = row.usual_px;
    out["lifetimeValue"] = row.lifetime_value;
    out["seven"] = row.seven_day;
    out["today"] = row.today;
    out["lifetime"] = row.lifetime;
  }
  std::string json;
  serializeJson(doc, json);
  return json;
}

bool decode_token_view(const std::string& json, TokenView& out) {
  if (json.empty() || json.size() > kTokenViewMaximumBytes) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  JsonVariantConst root = doc.as<JsonVariantConst>();
  if (root["schema"] != "rcink.token-view.v1" || !root["pick"].is<int>() ||
      root["pick"].as<int>() < -1 || root["pick"].as<int>() >= (int)kTokenProviderCount ||
      !root["rows"].is<JsonArrayConst>() || root["rows"].size() != kTokenProviderCount) return false;
  TokenView next;
  next.headline_pick = root["pick"].as<int8_t>();
  if (!small_string(root["headline"], next.headline) ||
      !small_string(root["footerChip"], next.footer_chip) ||
      !small_string(root["footer"], next.footer_text)) return false;
  for (size_t p = 0; p < kTokenProviderCount; p++) {
    JsonVariantConst input = root["rows"][p];
    TokenRowView& row = next.rows[p];
    if (!input["days"].is<JsonArrayConst>() || input["days"].size() != kTokenDays ||
        !input["bars"].is<JsonArrayConst>() || input["bars"].size() != kTokenDays ||
        !input["usual"].is<uint8_t>() || input["usual"].as<uint8_t>() > kTokenChartPx ||
        !input["lifetimeValue"].is<uint64_t>() ||
        !small_string(input["seven"], row.seven_day) ||
        !small_string(input["today"], row.today) ||
        !small_string(input["lifetime"], row.lifetime)) return false;
    row.usual_px = input["usual"].as<uint8_t>();
    row.lifetime_value = input["lifetimeValue"].as<uint64_t>();
    for (size_t i = 0; i < kTokenDays; i++) {
      if (!input["days"][i].is<uint64_t>() || !input["bars"][i].is<uint8_t>() ||
          input["bars"][i].as<uint8_t>() > kTokenChartPx) return false;
      row.shifted_days[i] = input["days"][i].as<uint64_t>();
      row.bars[i] = input["bars"][i].as<uint8_t>();
    }
  }
  out = next;
  return true;
}

PageRecord token_page(const TokenSnapshot& snapshot, uint64_t now, const TokenSync& sync) {
  PageRecord page;
  page.id = kTokenPageId;
  page.data_ts = now;
  page.layout_json = encode_token_view(derive_token_view(snapshot, now, sync));
  return page;
}

bool update_token_page(DeviceState& device, const TokenSnapshot& snapshot,
                       uint64_t now, const TokenSync& sync) {
  PageRecord page = token_page(snapshot, now, sync);
  for (PageRecord& existing : device.pages) {
    if (existing.id != kTokenPageId) continue;
    if (existing.layout_json == page.layout_json) return false;
    existing = std::move(page);
    return true;
  }
  device.pages.push_back(std::move(page));
  return true;
}

}  // namespace rcpaper
