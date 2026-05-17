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
};

void nvs_init();
DeviceState nvs_load();
void nvs_save(const DeviceState& s);
void nvs_save_index(uint8_t idx);

}  // namespace rcpaper
