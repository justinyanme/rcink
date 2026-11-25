#pragma once

#include <cstdint>
#include <string>

#include "input/buttons.h"
#include "input/navigation.h"
#include "render/monument.h"

namespace rcpaper {

enum class KeyLongAction : uint8_t {
  ToggleCarouselPin,
  CycleHeatmapWindow,
};

inline KeyLongAction key_long_action_for_page(View view,
                                              const std::string& active_page_id,
                                              bool usage_enabled) {
  if (usage_enabled && view == View::Deck &&
      (active_page_id == "desk-station" || active_page_id == kMonumentPageId)) {
    return KeyLongAction::CycleHeatmapWindow;
  }
  return KeyLongAction::ToggleCarouselPin;
}

inline bool interaction_changes_visible_state(const buttons::Events& events,
                                              bool runtime_mode_changed) {
  return events.key_tap || events.boot_tap || events.key_long ||
         events.boot_long || events.chord || runtime_mode_changed;
}

}  // namespace rcpaper
