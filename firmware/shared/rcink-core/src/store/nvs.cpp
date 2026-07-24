// src/store/nvs.cpp
#include "store/nvs.h"

#ifdef ARDUINO
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>

namespace rcpaper {

static const char* NS = "rcpaper";
// NVS key for the serialized pages blob. Stored via putBytes/getBytes — the
// Preferences string variants enforce a 4 KB cap (ESP_ERR_NVS_VALUE_TOO_LONG)
// that our ~15 KB pages JSON exceeds. Bytes have no such cap.
static const char* K_PAGES = "pages";
// Custom NVS partition (see partitions.csv). The default "nvs" partition is
// 20 KB — too tight for our pages blob plus wear-leveling slack. "rcdata" is
// 128 KB, dedicated to this app.
static const char* PART = "rcdata";

void nvs_init() { /* nothing; Preferences auto-init on begin() */ }

DeviceState nvs_load() {
  DeviceState s;
  Preferences p;
  bool opened = p.begin(NS, true, PART);
  s.page_index            = p.getUChar("idx", 0);
  // Keep the deployed key string stable while using the domain name in code.
  s.view_id               = p.getUChar("mode", 0);
  s.last_fetch_s          = p.getULong64("last", 0);
  s.last_fetch_fingerprint = p.getUInt("fp", 0);
  s.unchanged_fetches      = p.getUChar("unch", 0);
  s.last_rendered_id      = p.getString("rid", "").c_str();
  s.last_rendered_data_ts = p.getULong64("rts", 0);
  size_t blob_len = p.getBytesLength(K_PAGES);
  String blob;
  if (blob_len > 0) {
    std::vector<char> buf(blob_len + 1, 0);
    p.getBytes(K_PAGES, buf.data(), blob_len);
    blob = String(buf.data());
  }
  p.end();
  Serial.printf("[nvs] load opened=%d idx=%u view=%u last=%llu fp=%08x unch=%u blob_len=%u rid=%s rts=%llu\n",
                (int)opened, (unsigned)s.page_index, (unsigned)s.view_id,
                (unsigned long long)s.last_fetch_s,
                (unsigned)s.last_fetch_fingerprint, (unsigned)s.unchanged_fetches,
                (unsigned)blob_len, s.last_rendered_id.c_str(),
                (unsigned long long)s.last_rendered_data_ts);

  if (blob.length() == 0) return s;

  JsonDocument doc;
  if (deserializeJson(doc, blob)) return s;
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  if (arr.isNull()) return s;
  for (JsonVariantConst v : arr) {
    nvs_append_cached_page(v, s.pages);
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
  p.begin(NS, false, PART);
  // putBytes returns the number of bytes written; 0 means failure. The full
  // pages blob is ~15 KB on this dashboard and putString would silently fail
  // at the 4 KB NVS string ceiling — see K_PAGES comment above.
  size_t written = p.putBytes(K_PAGES, blob.c_str(), blob.length());
  p.putUChar("idx", s.page_index);
  p.putUChar("mode", s.view_id);
  p.putULong64("last", s.last_fetch_s);
  p.putUInt("fp", s.last_fetch_fingerprint);
  p.putUChar("unch", s.unchanged_fetches);
  p.end();
  if (written != blob.length()) {
    Serial.printf("[nvs] FAILED to save pages blob=%u B written=%u (idx=%u still saved)\n",
                  (unsigned)blob.length(), (unsigned)written, (unsigned)s.page_index);
  } else {
    Serial.printf("[nvs] saved %u pages, blob=%u B, idx=%u\n",
                  (unsigned)s.pages.size(), (unsigned)blob.length(),
                  (unsigned)s.page_index);
  }
}

void nvs_save_progress(uint8_t idx) {
  // Partial write for button-driven page navigation without forcing a full
  // pages-blob commit.
  Preferences p;
  p.begin(NS, false, PART);
  p.putUChar("idx", idx);
  p.end();
}

void nvs_save_view(uint8_t view_id) {
  Preferences p;
  p.begin(NS, false, PART);
  p.putUChar("mode", view_id);
  p.end();
}

void nvs_record_rendered_page(DeviceState& state, const PageRecord& page) {
  // Tiny partial write — keep the render-dedup tuple cheap to update every
  // render. Re-serializing the full pages blob (~15 KB) would defeat the
  // savings we just gained by skipping the panel refresh.
  update_render_marker(state, page);
  Preferences p;
  p.begin(NS, false, PART);
  p.putString("rid", state.last_rendered_id.c_str());
  p.putULong64("rts", state.last_rendered_data_ts);
  p.end();
}

}  // namespace rcpaper
#endif  // ARDUINO
