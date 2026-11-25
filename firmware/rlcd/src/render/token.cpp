#include "render/token.h"

#include <ArduinoJson.h>
#include <cstring>
#include <initializer_list>

namespace rcpaper {
namespace {
constexpr uint64_t kClockMinimum = 1704067200ULL;
constexpr uint64_t kClockMaximum = 4102444800ULL;

bool known_token_provider(const std::string& id) {
  return id == "claude" || id == "codex" || id == "gemini" || id == "kimi";
}

bool has_provider(const TokenSnapshot& snapshot, size_t count, const std::string& id) {
  for (size_t i = 0; i < count; i++) {
    if (snapshot.providers[i].id == id) return true;
  }
  return false;
}

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

bool fail(std::string* reason, const std::string& message) {
  if (reason) *reason = message;
  return false;
}

std::string row_tag(size_t p, const std::string& id = {}) {
  return id.empty() ? "row[" + std::to_string(p) + "]"
                    : "row[" + std::to_string(p) + "](" + id + ")";
}
}  // namespace

std::string token_provider_name(const std::string& id) {
  if (id == "claude") return "Claude";
  if (id == "codex") return "Codex";
  if (id == "gemini") return "Gemini";
  if (id == "kimi") return "Kimi";
  std::string name = id;
  if (!name.empty() && name[0] >= 'a' && name[0] <= 'z') {
    name[0] = (char)(name[0] - 'a' + 'A');
  }
  return name;
}

bool decode_token(const std::string& json, TokenSnapshot& out, std::string* reason) {
  if (reason) reason->clear();
  if (json.empty()) return fail(reason, "empty");
  if (json.size() > kTokenMaximumBytes) return fail(reason, "oversized");
  JsonDocument doc;
  JsonReader reader{json};
  const DeserializationError err =
      deserializeJson(doc, reader, DeserializationOption::NestingLimit(8));
  if (err) return fail(reason, std::string("json-parse:") + err.c_str());
  while (reader.pos < json.size()) {
    const char c = json[reader.pos++];
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
      return fail(reason, "trailing-data");
    }
  }
  JsonVariantConst root = doc.as<JsonVariantConst>();
  if (!exact(root, {"schema", "servedAt", "today", "requiredSourceCollectedAt", "providers"})) {
    return fail(reason, "root-fields");
  }
  if (root["schema"] != "rcink.token-usage.v2") return fail(reason, "schema");
  if (!epoch(root["servedAt"])) return fail(reason, "served-at");
  if (!root["today"].is<const char*>()) return fail(reason, "today-type");
  if (!root["requiredSourceCollectedAt"].isNull() &&
      !epoch(root["requiredSourceCollectedAt"])) {
    return fail(reason, "collected-at");
  }
  if (!root["providers"].is<JsonArrayConst>() ||
      root["providers"].size() > kTokenMaxProviders) {
    return fail(reason, "providers-array");
  }

  TokenSnapshot next;
  next.count = root["providers"].size();
  next.served_at = root["servedAt"].as<uint64_t>();
  next.today = root["today"].as<const char*>();
  if (!valid_date(next.today)) return fail(reason, "today-date");
  if (!root["requiredSourceCollectedAt"].isNull()) {
    next.required_source_collected_at = root["requiredSourceCollectedAt"].as<uint64_t>();
  }
  for (size_t p = 0; p < next.count; p++) {
    JsonVariantConst row = root["providers"][p];
    if (!exact(row, {"id", "days", "lifetime"}) || !row["id"].is<const char*>()) {
      return fail(reason, row_tag(p) + ":fields");
    }
    const std::string id = row["id"].as<const char*>();
    if (!known_token_provider(id)) return fail(reason, row_tag(p) + ":id");
    if (has_provider(next, p, id)) return fail(reason, row_tag(p, id) + ":dup-id");
    if (!row["days"].is<JsonArrayConst>() || row["days"].size() != kTokenDays) {
      return fail(reason, row_tag(p, id) + ":days");
    }
    if (!token_integer(row["lifetime"])) {
      return fail(reason, row_tag(p, id) + ":lifetime");
    }
    next.providers[p].id = id;
    next.providers[p].lifetime = row["lifetime"].as<uint64_t>();
    for (size_t i = 0; i < kTokenDays; i++) {
      if (!token_integer(row["days"][i])) {
        return fail(reason, row_tag(p, id) + ":day-val");
      }
      next.providers[p].days[i] = row["days"][i].as<uint64_t>();
    }
  }
  out = next;
  return true;
}

std::string encode_token(const TokenSnapshot& snapshot) {
  JsonDocument doc;
  doc["schema"] = "rcink.token-usage.v2";
  doc["servedAt"] = snapshot.served_at;
  doc["today"] = snapshot.today;
  if (snapshot.required_source_collected_at == 0) doc["requiredSourceCollectedAt"] = nullptr;
  else doc["requiredSourceCollectedAt"] = snapshot.required_source_collected_at;
  JsonArray providers = doc["providers"].to<JsonArray>();
  for (size_t p = 0; p < snapshot.count; p++) {
    JsonObject row = providers.add<JsonObject>();
    row["id"] = snapshot.providers[p].id;
    JsonArray days = row["days"].to<JsonArray>();
    for (uint64_t value : snapshot.providers[p].days) days.add(value);
    row["lifetime"] = snapshot.providers[p].lifetime;
  }
  std::string json;
  serializeJson(doc, json);
  return json;
}

bool token_content_equal(const TokenSnapshot& a, const TokenSnapshot& b) {
  if (a.today != b.today ||
      a.required_source_collected_at != b.required_source_collected_at ||
      a.count != b.count) return false;
  for (size_t p = 0; p < a.count; p++) {
    if (a.providers[p].id != b.providers[p].id ||
        a.providers[p].days != b.providers[p].days ||
        a.providers[p].lifetime != b.providers[p].lifetime) return false;
  }
  return true;
}

}  // namespace rcpaper
