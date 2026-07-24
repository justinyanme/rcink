#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "power/runtime_policy.h"

namespace rcpaper {

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
};

inline bool operator==(const DeckViewKey& lhs, const DeckViewKey& rhs) {
  return lhs.page_id == rhs.page_id && lhs.data_ts == rhs.data_ts &&
         lhs.page_index == rhs.page_index &&
         lhs.page_count == rhs.page_count &&
         lhs.battery_pct == rhs.battery_pct &&
         lhs.battery_ok == rhs.battery_ok && lhs.stale == rhs.stale &&
         lhs.runtime_mode == rhs.runtime_mode &&
         lhs.dark_mode == rhs.dark_mode;
}

inline bool operator!=(const DeckViewKey& lhs, const DeckViewKey& rhs) {
  return !(lhs == rhs);
}

}  // namespace rcpaper
