// src/store/nvs.cpp
#include "store/nvs.h"
#include <Preferences.h>
#include <ArduinoJson.h>

namespace rcpaper {

static const char* NS = "rcpaper";

void nvs_init() { /* nothing; Preferences auto-init on begin() */ }

DeviceState nvs_load() {
  DeviceState s;
  Preferences p;
  p.begin(NS, true);
  s.page_index   = p.getUChar("idx", 0);
  s.last_fetch_s = p.getULong64("last", 0);
  String blob = p.getString("pages", "");
  p.end();

  if (blob.length() == 0) return s;

  JsonDocument doc;
  if (deserializeJson(doc, blob)) return s;
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  if (arr.isNull()) return s;
  for (JsonVariantConst v : arr) {
    PageRecord rec;
    rec.id          = (const char*)(v["id"] | "");
    rec.order       = v["order"] | 0;
    rec.data_ts     = (uint64_t)(v["data_ts"] | (uint64_t)0);
    rec.layout_json = (const char*)(v["layout_json"] | "");
    s.pages.push_back(std::move(rec));
  }
  return s;
}

void nvs_save(const DeviceState& s) {
  // Re-serialize pages to a compact JSON blob.
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& rec : s.pages) {
    JsonObject o      = arr.add<JsonObject>();
    o["id"]           = rec.id.c_str();
    o["order"]        = rec.order;
    o["data_ts"]      = (uint64_t)rec.data_ts;
    o["layout_json"]  = rec.layout_json.c_str();
  }
  String blob;
  serializeJson(doc, blob);

  Preferences p;
  p.begin(NS, false);
  p.putString("pages", blob);
  p.putUChar("idx", s.page_index);
  p.putULong64("last", s.last_fetch_s);
  p.end();
  Serial.printf("[nvs] saved %u pages, blob=%u B, idx=%u\n",
                (unsigned)s.pages.size(), blob.length(), s.page_index);
}

void nvs_save_index(uint8_t idx) {
  Preferences p;
  p.begin(NS, false);
  p.putUChar("idx", idx);
  p.end();
}

}  // namespace rcpaper
