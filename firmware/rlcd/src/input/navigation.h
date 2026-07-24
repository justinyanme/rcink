#pragma once

#include <cstddef>
#include <cstdint>

namespace rcpaper {

enum class Mode : uint8_t { Dashboard, Deck };

struct NavigationState {
  Mode mode;
  size_t page_index;
};

inline NavigationState navigation_after_key_press(
    Mode mode, size_t page_index, size_t page_count) {
  if (page_count == 0) return {Mode::Dashboard, 0};
  if (mode == Mode::Dashboard) {
    return {Mode::Deck, page_index < page_count ? page_index : 0};
  }
  if (page_index + 1 < page_count) return {Mode::Deck, page_index + 1};
  return {Mode::Dashboard, 0};
}

}  // namespace rcpaper
