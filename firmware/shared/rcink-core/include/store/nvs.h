// src/store/nvs.h
#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "proto/envelope.h"  // for PageRecord

namespace rcpaper {

struct DeviceState {
  std::vector<PageRecord> pages;
  uint8_t  page_index   = 0;
  uint8_t  view_id      = 0;   // target-local View id; 0 means default View
  uint64_t last_fetch_s = 0;   // epoch seconds; 0 = never
  uint32_t last_fetch_fingerprint = 0;  // fingerprint of last successful page set
  uint8_t  unchanged_fetches = 0;       // consecutive successful fetches with no content change

  // Render-dedup marker: id + data_ts of the most recent successful render.
  // Persisted so a wake-from-deep-sleep can compare without re-rendering.
  // Empty `last_rendered_id` = never rendered.
  std::string last_rendered_id;
  uint64_t last_rendered_data_ts = 0;
};

void nvs_init();
DeviceState nvs_load();
void nvs_save(const DeviceState& s);
void nvs_save_progress(uint8_t idx);
void nvs_save_view(uint8_t view_id);
void nvs_save_render_marker(const std::string& id, uint64_t data_ts);

inline bool nvs_cached_page_is_valid(const PageRecord& rec) {
  return !rec.id.empty() && !rec.layout_json.empty();
}

inline bool nvs_append_cached_page(JsonVariantConst v, std::vector<PageRecord>& pages) {
  PageRecord rec;
  rec.id          = (const char*)(v["id"] | "");
  rec.order       = v["order"] | 0;
  rec.data_ts     = (uint64_t)(v["data_ts"] | (uint64_t)0);
  rec.layout_json = (const char*)(v["layout_json"] | "");
  if (!nvs_cached_page_is_valid(rec)) return false;
  pages.push_back(std::move(rec));
  return true;
}

}  // namespace rcpaper
