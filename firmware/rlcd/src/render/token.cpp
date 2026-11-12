#include "render/token.h"

#include <ArduinoJson.h>
#include <cstring>
#include <initializer_list>

namespace rcpaper {
namespace {
const char* const kIds[kTokenProviderCount] = {"claude", "codex", "gemini"};
constexpr uint64_t kClockMinimum = 1704067200ULL;
constexpr uint64_t kClockMaximum = 4102444800ULL;

bool exact(JsonVariantConst value, std::initializer_list<const char*> fields) {
  if (!value.is<JsonObjectConst>() || value.size() != fields.size()) return false;
  for (const char* field : fields) {
    if (value.as<JsonObjectConst>()[field].isUnbound()) return false;
  }
  return true;
}

bool epoch(JsonVariantConst value) {
  return value.is<uint64_t>() && value.as<uint64_t>() >= kClockMinimum &&
         value.as<uint64_t>() < kClockMaximum;
}

bool token_integer(JsonVariantConst value) {
  return value.is<uint64_t>();
}

bool leap_year(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool valid_date(const std::string& date) {
  if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
  for (size_t i = 0; i < date.size(); i++) {
    if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) return false;
  }
  const int year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
                   (date[2] - '0') * 10 + date[3] - '0';
  const int month = (date[5] - '0') * 10 + date[6] - '0';
  const int day = (date[8] - '0') * 10 + date[9] - '0';
  if (year < 2024 || month < 1 || month > 12 || day < 1) return false;
  const int lengths[] = {31, leap_year(year) ? 29 : 28, 31, 30, 31, 30,
                         31, 31, 30, 31, 30, 31};
  return day <= lengths[month - 1];
}

// ArduinoJson accepts a valid JSON prefix; retain the cursor to reject garbage.
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
}  // namespace

bool decode_token(const std::string& json, TokenSnapshot& out) {
  if (json.empty() || json.size() > kTokenMaximumBytes) return false;
  JsonDocument doc;
  JsonReader reader{json};
  if (deserializeJson(doc, reader, DeserializationOption::NestingLimit(8))) return false;
  while (reader.pos < json.size()) {
    const char c = json[reader.pos++];
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') return false;
  }
  JsonVariantConst root = doc.as<JsonVariantConst>();
  if (!exact(root, {"schema", "servedAt", "today", "requiredSourceCollectedAt", "providers"}) ||
      root["schema"] != "rcink.token-usage.v1" || !epoch(root["servedAt"]) ||
      !root["today"].is<const char*>() ||
      (!root["requiredSourceCollectedAt"].isNull() && !epoch(root["requiredSourceCollectedAt"])) ||
      !root["providers"].is<JsonArrayConst>() ||
      root["providers"].size() != kTokenProviderCount) return false;

  TokenSnapshot next;
  next.served_at = root["servedAt"].as<uint64_t>();
  next.today = root["today"].as<const char*>();
  if (!valid_date(next.today)) return false;
  if (!root["requiredSourceCollectedAt"].isNull()) {
    next.required_source_collected_at = root["requiredSourceCollectedAt"].as<uint64_t>();
  }
  for (size_t p = 0; p < kTokenProviderCount; p++) {
    JsonVariantConst row = root["providers"][p];
    if (!exact(row, {"id", "days", "lifetime"}) || row["id"] != kIds[p] ||
        !row["days"].is<JsonArrayConst>() || row["days"].size() != kTokenDays ||
        !token_integer(row["lifetime"])) return false;
    next.providers[p].lifetime = row["lifetime"].as<uint64_t>();
    for (size_t i = 0; i < kTokenDays; i++) {
      if (!token_integer(row["days"][i])) return false;
      next.providers[p].days[i] = row["days"][i].as<uint64_t>();
    }
  }
  out = next;
  return true;
}

std::string encode_token(const TokenSnapshot& snapshot) {
  JsonDocument doc;
  doc["schema"] = "rcink.token-usage.v1";
  doc["servedAt"] = snapshot.served_at;
  doc["today"] = snapshot.today;
  if (snapshot.required_source_collected_at == 0) doc["requiredSourceCollectedAt"] = nullptr;
  else doc["requiredSourceCollectedAt"] = snapshot.required_source_collected_at;
  JsonArray providers = doc["providers"].to<JsonArray>();
  for (size_t p = 0; p < kTokenProviderCount; p++) {
    JsonObject row = providers.add<JsonObject>();
    row["id"] = kIds[p];
    JsonArray days = row["days"].to<JsonArray>();
    for (uint64_t value : snapshot.providers[p].days) days.add(value);
    row["lifetime"] = snapshot.providers[p].lifetime;
  }
  std::string json;
  serializeJson(doc, json);
  return json;
}

}  // namespace rcpaper
