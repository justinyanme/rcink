#include "render/usage.h"

#include "render/usage_view.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <utility>

namespace rcpaper {
namespace {
const char* kIds[] = {"claude", "codex", "kimi", "gemini"};
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
std::string precise_percent(double value) {
  char buffer[32];
  // Default JSON decimal truncation can move a saved value across .5 rounding.
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  return buffer;
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

std::string encode_usage(const UsageSnapshot& snapshot) {
  JsonDocument doc;
  doc["schema"] = "rcink.usage-limits.v1";
  doc["servedAt"] = snapshot.served_at;
  JsonArray rows = doc["providers"].to<JsonArray>();
  for (size_t i = 0; i < 4; i++) {
    const auto& provider = snapshot.providers[i];
    JsonObject row = rows.add<JsonObject>();
    row["id"] = kIds[i];
    if (provider.status == UsageStatus::Unavailable) {
      row["status"] = "unavailable";
      for (auto field : {"usedPercent", "remainingPercent", "fetchedAt", "expiresAt", "resetsAt"}) row[field] = nullptr;
      continue;
    }
    const bool stale = provider.status == UsageStatus::Stale || snapshot.served_at >= provider.expires_at ||
        (provider.resets_at != 0 && snapshot.served_at >= provider.resets_at);
    row["status"] = stale ? "stale" : "ok";
    row["usedPercent"] = serialized(precise_percent(provider.used_percent));
    row["remainingPercent"] = serialized(precise_percent(100 - provider.used_percent));
    row["fetchedAt"] = provider.fetched_at;
    row["expiresAt"] = provider.expires_at;
    if (provider.resets_at) row["resetsAt"] = provider.resets_at;
    else row["resetsAt"] = nullptr;
  }
  std::string json;
  serializeJson(doc, json);
  return json;
}

bool merge_usage(const UsageSnapshot& incoming, UsageSnapshot& cache) {
  bool changed = false;
  for (size_t i = 0; i < 4; i++) {
    if (incoming.providers[i].status != UsageStatus::Unavailable &&
        incoming.providers[i].fetched_at > cache.providers[i].fetched_at) {
      cache.providers[i] = incoming.providers[i];
      changed = true;
    } else if (cache.providers[i].status == UsageStatus::Ok &&
               (incoming.providers[i].status == UsageStatus::Unavailable ||
                (incoming.providers[i].fetched_at == cache.providers[i].fetched_at &&
                 incoming.providers[i].status == UsageStatus::Stale))) {
      // Later authenticated reads may know a reset/expiry before the local RTC.
      cache.providers[i].status = UsageStatus::Stale;
      changed = true;
    }
  }
  if (changed) cache.served_at = std::max(cache.served_at, incoming.served_at);
  return changed;
}

PageRecord usage_page(const UsageSnapshot& snapshot, uint64_t now, const UsageSync& sync) {
  PageRecord page;
  page.id = kUsagePageId;
  page.data_ts = now;
  page.layout_json = encode_usage_view(derive_usage_view(snapshot, now, sync));
  return page;
}

bool update_usage_page(DeviceState& device, const UsageSnapshot& snapshot,
                       uint64_t now, const UsageSync& sync) {
  PageRecord page = usage_page(snapshot, now, sync);
  if (device.pages.size() == 1 && device.pages[0].id == page.id &&
      device.pages[0].layout_json == page.layout_json) return false;
  device.pages = {std::move(page)};
  device.page_index = 0;
  return true;
}

uint32_t usage_page_fingerprint(const PageRecord& page) {
  uint32_t hash = 2166136261UL;
  for (const unsigned char byte : page.layout_json) hash = (hash ^ byte) * 16777619UL;
  return hash;
}

}  // namespace rcpaper
