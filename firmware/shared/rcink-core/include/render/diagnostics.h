#pragma once
#include <cstdint>
#include "proto/schema.h"

namespace rcpaper {

struct DiagnosticsSnapshot {
  uint32_t free_heap = 0;
  uint32_t free_psram = 0;
  bool     battery_ok = false;
  uint8_t  battery_pct = 0;
  uint16_t battery_mv = 0;
  bool     wifi_connected = false;
  int32_t  rssi_dbm = 0;
  uint64_t last_fetch_s = 0;
  uint32_t data_fingerprint = 0;
  uint8_t  unchanged_fetches = 0;
  uint32_t wake_count = 0;
  const char* last_result = "";
};

LayoutNode build_diagnostics_page(const DiagnosticsSnapshot& snap);

}  // namespace rcpaper
