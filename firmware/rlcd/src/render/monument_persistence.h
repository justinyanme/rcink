#pragma once

#include "render/monument.h"

namespace rcpaper {

// Keep storage out of the pure codec so native tests need not link NVS.
inline bool persist_monument_snapshot(const MonumentSnapshot& snapshot) {
  MonumentSnapshot stored;
  if (decode_monument(nvs_load_monument(), stored) &&
      monument_content_equal(stored, snapshot)) {
    return true;
  }
  return nvs_save_monument(encode_monument(snapshot));
}

inline HeatmapWindow load_heatmap_window() {
  return heatmap_window_from_u8(
      nvs_load_heatmap_window(heatmap_window_to_u8(HeatmapWindow::Year)));
}

inline void save_heatmap_window(HeatmapWindow window) {
  const uint8_t raw = heatmap_window_to_u8(window);
  if (nvs_load_heatmap_window(255) == raw) return;
  nvs_save_heatmap_window(raw);
}

inline HeatmapWindow load_station_heatmap_window() {
  const uint8_t raw = nvs_load_station_heatmap_window(
      heatmap_window_to_u8(HeatmapWindow::SixMonths));
  if (raw > 2) return HeatmapWindow::SixMonths;
  return heatmap_window_from_u8(raw);
}

inline void save_station_heatmap_window(HeatmapWindow window) {
  const uint8_t raw = heatmap_window_to_u8(window);
  if (nvs_load_station_heatmap_window(255) == raw) return;
  nvs_save_station_heatmap_window(raw);
}

inline bool load_desk_cat_enabled() {
  return nvs_load_desk_cat_enabled(true);
}

inline void save_desk_cat_enabled(bool enabled) {
  nvs_save_desk_cat_enabled(enabled);
}

}  // namespace rcpaper
