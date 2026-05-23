// src/store/nvs.h
#pragma once
#include <Arduino.h>
#include <vector>
#include "net/axiom.h"   // for PageRecord

namespace rcpaper {

struct DeviceState {
  std::vector<PageRecord> pages;
  uint8_t  page_index   = 0;
  uint64_t last_fetch_s = 0;   // epoch seconds; 0 = never

  // Render-dedup marker: id + data_ts of the most recent successful render.
  // Persisted so a wake-from-deep-sleep can compare without re-rendering.
  // Empty `last_rendered_id` = never rendered.
  String   last_rendered_id;
  uint64_t last_rendered_data_ts = 0;
};

void nvs_init();
DeviceState nvs_load();
void nvs_save(const DeviceState& s);
void nvs_save_progress(uint8_t idx);
void nvs_save_render_marker(const String& id, uint64_t data_ts);

}  // namespace rcpaper
