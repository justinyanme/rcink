#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "power/runtime_policy.h"
#include "render/usage.h"

namespace rcpaper {

// Only frames actually shown on the panel can suppress a later ECO redraw.
inline bool record_deck_frame(bool drawn, DeviceState& state, const PageRecord& page,
                              uint32_t* usage_fingerprint) {
  if (!drawn) return false;
  if (usage_fingerprint) *usage_fingerprint = usage_page_fingerprint(page);
  if (!render_marker_matches(state, page)) nvs_record_rendered_page(state, page);
  return true;
}

struct DeckViewKey {
  std::string page_id;
  uint64_t data_ts;
  size_t page_index;
  size_t page_count;
  uint8_t battery_pct;
  bool battery_ok;
  bool stale;
  RuntimeMode runtime_mode;
  bool dark_mode;
  bool carousel_pinned;
  int16_t env_temp_decideg;
  uint8_t env_rh_pct;
  bool env_ok;
};

inline bool operator==(const DeckViewKey& lhs, const DeckViewKey& rhs) {
  return lhs.page_id == rhs.page_id && lhs.data_ts == rhs.data_ts &&
         lhs.page_index == rhs.page_index &&
         lhs.page_count == rhs.page_count &&
         lhs.battery_pct == rhs.battery_pct &&
         lhs.battery_ok == rhs.battery_ok && lhs.stale == rhs.stale &&
         lhs.runtime_mode == rhs.runtime_mode &&
         lhs.dark_mode == rhs.dark_mode &&
         lhs.carousel_pinned == rhs.carousel_pinned &&
         lhs.env_temp_decideg == rhs.env_temp_decideg &&
         lhs.env_rh_pct == rhs.env_rh_pct && lhs.env_ok == rhs.env_ok;
}

inline bool operator!=(const DeckViewKey& lhs, const DeckViewKey& rhs) {
  return !(lhs == rhs);
}

}  // namespace rcpaper
