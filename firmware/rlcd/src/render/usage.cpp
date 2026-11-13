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
bool provider_id(JsonVariantConst value, std::string& out) {
  if (!value.is<const char*>()) return false;
  out = value.as<const char*>();
  if (out.empty() || out.size() > kUsageMaxIdLength) return false;
  for (const char c : out) if (c < '!' || c > '~') return false;  // printable ASCII
  return true;
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

size_t find_provider(const UsageSnapshot& snapshot, const std::string& id) {
  for (size_t i = 0; i < snapshot.count; i++)
    if (snapshot.providers[i].id == id) return i;
  return snapshot.count;
}
}  // namespace

std::string usage_provider_name(const std::string& id) {
  if (id == "claude") return "Claude";
  if (id == "codex") return "Codex";
  if (id == "kimi") return "Kimi";
  if (id == "gemini") return "Gemini";
  std::string name = id;
  if (name[0] >= 'a' && name[0] <= 'z') name[0] = (char)(name[0] - 'a' + 'A');
  return name;
}

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
  if (!exact(root, {"schema", "servedAt", "providers"}) || root["schema"] != "rcink.usage-limits.v2" ||
      !epoch(root["servedAt"]) || !root["providers"].is<JsonArrayConst>() ||
      root["providers"].size() > kUsageMaxProviders) return false;
  UsageSnapshot next;
  next.count = root["providers"].size();
  next.served_at = root["servedAt"].as<uint64_t>();
  for (size_t i = 0; i < next.count; i++) {
    const auto row = root["providers"][i];
    std::string id;
    if (!exact(row, {"id", "status", "usedPercent", "remainingPercent", "fetchedAt", "expiresAt", "resetsAt"}) ||
        !provider_id(row["id"], id) || find_provider(next, id) < next.count) return false;
    auto& target = next.providers[i];
    target.id = id;
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
  doc["schema"] = "rcink.usage-limits.v2";
  doc["servedAt"] = snapshot.served_at;
  JsonArray rows = doc["providers"].to<JsonArray>();
  for (size_t i = 0; i < snapshot.count; i++) {
    const auto& provider = snapshot.providers[i];
    JsonObject row = rows.add<JsonObject>();
    row["id"] = provider.id;
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
  // Accounts the source no longer reports drop out of the cache (and the
  // frame) instead of lingering as forever-stale rows.
  for (size_t i = 0; i < cache.count;) {
    if (find_provider(incoming, cache.providers[i].id) < incoming.count) { i++; continue; }
    for (size_t j = i + 1; j < cache.count; j++) cache.providers[j - 1] = cache.providers[j];
    cache.providers[--cache.count] = UsageProvider{};
    changed = true;
  }
  for (size_t i = 0; i < incoming.count; i++) {
    const UsageProvider& next = incoming.providers[i];
    const size_t slot = find_provider(cache, next.id);
    if (slot == cache.count) {
      // New account upstream: show it immediately, even before its first
      // successful observation.
      if (cache.count < kUsageMaxProviders) {
        cache.providers[cache.count++] = next;
        changed = true;
      }
      continue;
    }
    UsageProvider& current = cache.providers[slot];
    if (next.status != UsageStatus::Unavailable &&
        next.fetched_at > current.fetched_at) {
      current = next;
      changed = true;
    } else if (current.status == UsageStatus::Ok &&
               (next.status == UsageStatus::Unavailable ||
                (next.fetched_at == current.fetched_at &&
                 next.status == UsageStatus::Stale))) {
      // Later authenticated reads may know a reset/expiry before the local RTC.
      current.status = UsageStatus::Stale;
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
  for (PageRecord& existing : device.pages) {
    if (existing.id != kUsagePageId) continue;
    if (existing.layout_json == page.layout_json) return false;
    existing = std::move(page);
    return true;  // Preserve the visible page when Token is also present.
  }
  device.pages.insert(device.pages.begin(), std::move(page));
  return true;
}

uint32_t usage_page_fingerprint(const PageRecord& page) {
  uint32_t hash = 2166136261UL;
  for (const unsigned char byte : page.layout_json) hash = (hash ^ byte) * 16777619UL;
  return hash;
}

uint64_t usage_refetch_at_s(const UsageSnapshot& snapshot, uint64_t last_fetch_s) {
  if (last_fetch_s == 0) return 0;
  uint64_t next = 0;
  for (size_t i = 0; i < snapshot.count; i++) {
    const UsageProvider& p = snapshot.providers[i];
    if (p.status == UsageStatus::Unavailable || p.fetched_at == 0) continue;
    uint64_t at = 0;
    if (p.expires_at > last_fetch_s) {
      at = p.expires_at + kUsageExpiryGraceS;  // fresh at the last fetch: refetch as it expires
    } else if (last_fetch_s < p.fetched_at + kUsageChaseWindowS) {
      at = last_fetch_s + kUsageChaseRetryS;  // the source is late: check back soon
    } else {
      continue;  // long quiet: the normal interval is enough
    }
    if (next == 0 || at < next) next = at;
  }
  return next;
}

}  // namespace rcpaper
