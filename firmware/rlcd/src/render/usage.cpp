#include "render/usage.h"

#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <initializer_list>

namespace rcpaper {
namespace {
const char* kIds[] = {"claude", "codex", "kimi", "gemini"};
const char* kNames[] = {"Claude", "Codex", "Kimi", "Gemini (Antigravity)"};
constexpr uint64_t kClockMinimum = 1704067200;

bool exact(JsonVariantConst value, std::initializer_list<const char*> fields) {
  if (!value.is<JsonObjectConst>() || value.size() != fields.size()) return false;
  for (auto field : fields) if (value.as<JsonObjectConst>()[field].isUnbound()) return false;
  return true;
}
bool epoch(JsonVariantConst value) {
  return value.is<uint64_t>() && value.as<uint64_t>() >= kClockMinimum &&
         value.as<uint64_t>() < 4102444800ULL;
}
bool percent(JsonVariantConst value) {
  return value.is<double>() && std::isfinite(value.as<double>()) &&
         value.as<double>() >= 0 && value.as<double>() <= 100;
}

// ArduinoJson accepts a valid JSON prefix; retain the cursor to reject trailing garbage.
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
void text(JsonArray children, const std::string& value, const char* size, bool bold = false) {
  JsonObject node = children.add<JsonObject>();
  node["type"] = "text"; node["value"] = value; node["size"] = size;
  if (bold) node["weight"] = "bold";
}
std::string utc_stamp(uint64_t epoch_s) {
  const time_t instant = (time_t)epoch_s;
  struct tm utc = {};
#ifdef ARDUINO
  gmtime_r(&instant, &utc);
#else
  utc = *std::gmtime(&instant);
#endif
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02dZ", utc.tm_mon + 1,
                utc.tm_mday, utc.tm_hour, utc.tm_min);
  return buf;
}
}  // namespace

bool decode_usage(const std::string& json, UsageSnapshot& out) {
  if (json.empty() || json.size() > kUsageMaximumBytes) return false;
  JsonDocument doc;
  JsonReader reader{json};
  if (deserializeJson(doc, reader, DeserializationOption::NestingLimit(8))) return false;
  while (reader.pos < json.size()) {
    const char c = json[reader.pos++];
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') return false;
  }
  JsonVariantConst root = doc.as<JsonVariantConst>();
  if (!exact(root, {"schema", "servedAt", "providers"}) || root["schema"] != "rcink.usage-limits.v1" ||
      !epoch(root["servedAt"]) || !root["providers"].is<JsonArrayConst>() || root["providers"].size() != 4) return false;
  UsageSnapshot next;
  next.served_at = root["servedAt"].as<uint64_t>();
  for (size_t i = 0; i < 4; i++) {
    const auto row = root["providers"][i];
    if (!exact(row, {"id", "status", "usedPercent", "remainingPercent", "fetchedAt", "expiresAt", "resetsAt"}) ||
        row["id"] != kIds[i]) return false;
    auto& target = next.providers[i];
    if (row["status"] == "unavailable") {
      for (auto field : {"usedPercent", "remainingPercent", "fetchedAt", "expiresAt", "resetsAt"}) {
        if (!row[field].isNull()) return false;
      }
      continue;
    }
    if (row["status"] != "ok" && row["status"] != "stale") return false;
    if (!percent(row["usedPercent"]) || !percent(row["remainingPercent"]) ||
        std::fabs(row["usedPercent"].as<double>() + row["remainingPercent"].as<double>() - 100) > 0.000001 ||
        !epoch(row["fetchedAt"]) || !epoch(row["expiresAt"]) ||
        (!row["resetsAt"].isNull() && !epoch(row["resetsAt"]))) return false;
    target.status = row["status"] == "ok" ? UsageStatus::Ok : UsageStatus::Stale;
    target.used_percent = row["usedPercent"].as<double>();
    target.fetched_at = row["fetchedAt"].as<uint64_t>();
    target.expires_at = row["expiresAt"].as<uint64_t>();
    target.resets_at = row["resetsAt"].isNull() ? 0 : row["resetsAt"].as<uint64_t>();
    if (target.fetched_at > next.served_at + 300 || target.expires_at <= target.fetched_at ||
        (target.resets_at != 0 && target.resets_at <= target.fetched_at)) return false;
    if (target.status == UsageStatus::Ok && (next.served_at >= target.expires_at ||
        (target.resets_at != 0 && next.served_at >= target.resets_at))) return false;
  }
  out = next;
  return true;
}

PageRecord usage_page(const UsageSnapshot& snapshot, uint64_t now, bool fetch_failed) {
  JsonDocument doc;
  doc["type"] = "vstack"; doc["pad"] = 12; doc["gap"] = 10;
  JsonArray children = doc["children"].to<JsonArray>();
  text(children, fetch_failed ? "Weekly / fetch failed" : "Weekly allowance", "body", true);
  if (now < kClockMinimum) text(children, "Clock unknown / freshness unknown", "sm");
  for (size_t i = 0; i < 4; i++) {
    const auto& provider = snapshot.providers[i];
    JsonObject row = children.add<JsonObject>();
    row["type"] = "vstack"; row["gap"] = 4;
    JsonArray parts = row["children"].to<JsonArray>();
    JsonObject heading = parts.add<JsonObject>();
    heading["type"] = "hstack"; heading["gap"] = 8; heading["align"] = "center";
    JsonArray labels = heading["children"].to<JsonArray>();
    text(labels, kNames[i], "sm", true);
    if (provider.status == UsageStatus::Unavailable) {
      text(labels, "--", "h2");
      continue;
    }
    const int used = (int)std::round(provider.used_percent);
    const int remaining = 100 - used;
    text(labels, std::to_string(remaining) + "% left", "h2", true);
    const bool stale = provider.status == UsageStatus::Stale || now < kClockMinimum ||
        now + 300 < provider.fetched_at || now >= provider.expires_at ||
        (provider.resets_at != 0 && now >= provider.resets_at);
    text(parts, std::to_string(used) + "% used" + (stale ? " / STALE" : ""), "sm");
    JsonObject bar = parts.add<JsonObject>();
    // Stay above draw_hbar's 4px cutoff after RLCD spacing compaction.
    bar["type"] = "hbar"; bar["pct"] = remaining; bar["height"] = 12;
    text(parts, "At " + utc_stamp(provider.fetched_at) + " reset " +
         (provider.resets_at == 0 ? "--" : utc_stamp(provider.resets_at)), "sm");
  }
  PageRecord page;
  page.id = "usage-limits";
  page.data_ts = now;
  serializeJson(doc, page.layout_json);
  return page;
}

}  // namespace rcpaper
