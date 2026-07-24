#pragma once

#include "input/buttons.h"

namespace rcpaper {

inline bool interaction_changes_visible_state(const buttons::Events& events,
                                              bool runtime_mode_changed) {
  return events.key_tap || events.boot_tap || runtime_mode_changed;
}

}  // namespace rcpaper
