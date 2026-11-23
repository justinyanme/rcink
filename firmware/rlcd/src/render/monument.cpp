#include "render/monument.h"

#include <ArduinoJson.h>

#include <cstdint>
#include <limits>

namespace rcpaper {
namespace {

bool fail(std::string* reason, const char* why) {
  if (reason) *reason = why;
  return false;
}

bool valid_date(const std::string& value, int& year, int& month, int& day) {
  if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
  for (size_t i = 0; i < 10; i++) {
    if (i == 4 || i == 7) continue;
    if (value[i] < '0' || value[i] > '9') return false;
  }
  year = (value[0] - '0') * 1000 + (value[1] - '0') * 100 +
         (value[2] - '0') * 10 + (value[3] - '0');
  month = (value[5] - '0') * 10 + (value[6] - '0');
  day = (value[8] - '0') * 10 + (value[9] - '0');
  if (year < 2020 || year > 2100 || month < 1 || month > 12 || day < 1) {
    return false;
  }
  static const int kMonthDays[] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  int limit = kMonthDays[month - 1];
  if (month == 2 && leap) limit = 29;
  return day <= limit;
}

int64_t civil_days(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = (unsigned)(year - era * 400);
  const unsigned doy =
      (153 * (month + (month > 2 ? (unsigned)-3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

struct JsonReader {
  const std::string& value;
  size_t pos = 0;
  explicit JsonReader(const std::string& input) : value(input) {}
  int read() { return pos < value.size() ? (unsigned char)value[pos++] : -1; }
  size_t readBytes(char* dest, size_t count) {
    const size_t available = value.size() - pos;
    if (count > available) count = available;
    memcpy(dest, value.data() + pos, count);
    pos += count;
    return count;
  }
};

bool valid_integer_tokens(JsonVariantConst v, uint64_t& out) {
  if (!v.is<uint64_t>()) return false;
  out = v.as<uint64_t>();
  return true;
}

}  // namespace

bool decode_monument(const std::string& json, MonumentSnapshot& out,
                     std::string* reason) {
  if (json.empty()) return fail(reason, "empty");
  if (json.size() > kMonumentMaximumBytes) return fail(reason, "oversized");
  JsonDocument doc;
  JsonReader reader(json);
  const DeserializationError err = deserializeJson(doc, reader);
  if (err) return fail(reason, "json");
  if (doc.overflowed()) return fail(reason, "overflow");
  for (size_t i = reader.pos; i < json.size(); i++) {
    if (json[i] != ' ' && json[i] != '\t' && json[i] != '\n' &&
        json[i] != '\r') {
      return fail(reason, "trailing-data");
    }
  }

  JsonVariantConst root = doc.as<JsonVariantConst>();
  if (!root.is<JsonObjectConst>()) return fail(reason, "root");
  if (!root["startDate"].is<const char*>() || !root["endDate"].is<const char*>()) {
    return fail(reason, "dates");
  }

  MonumentSnapshot next;
  next.start_date = root["startDate"].as<const char*>();
  next.end_date = root["endDate"].as<const char*>();
  int sy = 0, sm = 0, sd = 0, ey = 0, em = 0, ed = 0;
  if (!valid_date(next.start_date, sy, sm, sd) ||
      !valid_date(next.end_date, ey, em, ed)) {
    return fail(reason, "date-format");
  }
  const int64_t start_c = civil_days(sy, (unsigned)sm, (unsigned)sd);
  const int64_t end_c = civil_days(ey, (unsigned)em, (unsigned)ed);
  if (end_c - start_c != (int64_t)(kMonumentDays - 1)) {
    return fail(reason, "span");
  }

  if (!root["days"].is<JsonArrayConst>()) return fail(reason, "days-array");
  JsonArrayConst arr = root["days"].as<JsonArrayConst>();

  const bool is_compact = root["schema"].is<const char*>();
  if (is_compact) {
    if (root["schema"] != "rcink.monument.v1") return fail(reason, "schema");
    if (root.as<JsonObjectConst>().size() != 4) return fail(reason, "root-fields");
    if (arr.size() != kMonumentDays) return fail(reason, "days-len");
    for (size_t i = 0; i < kMonumentDays; i++) {
      if (!valid_integer_tokens(arr[i], next.days[i])) {
        return fail(reason, "day-val");
      }
    }
  } else {
    if (root.as<JsonObjectConst>().size() != 3) return fail(reason, "root-fields");
    if (arr.size() > kMonumentDays) return fail(reason, "days-len");
    for (JsonVariantConst item : arr) {
      if (!item.is<JsonObjectConst>() ||
          !item["date"].is<const char*>()) {
        return fail(reason, "day-entry");
      }
      int y = 0, m = 0, d = 0;
      const std::string date_str = item["date"].as<const char*>();
      if (!valid_date(date_str, y, m, d)) return fail(reason, "day-date");
      const int64_t dc = civil_days(y, (unsigned)m, (unsigned)d);
      if (dc < start_c || dc > end_c) return fail(reason, "day-range");
      uint64_t val = 0;
      if (!valid_integer_tokens(item["totalTokens"], val)) {
        return fail(reason, "day-val");
      }
      next.days[(size_t)(dc - start_c)] = val;
    }
  }

  out = next;
  if (reason) reason->clear();
  return true;
}

std::string encode_monument(const MonumentSnapshot& snapshot) {
  JsonDocument doc;
  doc["schema"] = "rcink.monument.v1";
  doc["startDate"] = snapshot.start_date;
  doc["endDate"] = snapshot.end_date;
  JsonArray days = doc["days"].to<JsonArray>();
  for (size_t i = 0; i < kMonumentDays; i++) {
    days.add(i < snapshot.days.size() ? snapshot.days[i] : 0ULL);
  }
  std::string json;
  serializeJson(doc, json);
  return json;
}

bool monument_content_equal(const MonumentSnapshot& a,
                            const MonumentSnapshot& b) {
  return a.start_date == b.start_date && a.end_date == b.end_date &&
         a.days == b.days;
}

}  // namespace rcpaper
