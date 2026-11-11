#pragma once

#include <cstddef>
#include <cstdint>

namespace rcpaper {

enum class View : uint8_t { Dashboard, Deck };

struct NavigationState {
  View view;
  size_t page_index;
};

inline uint8_t startup_view_id(uint8_t saved_view_id, bool usage_enabled,
                               bool view_saved) {
  return usage_enabled && !view_saved ? 1 : saved_view_id;
}

inline NavigationState navigation_after_next_press(
    View view, size_t page_index, size_t page_count) {
  if (page_count == 0) return {View::Dashboard, 0};
  if (view == View::Dashboard) return {View::Deck, 0};
  if (page_index >= page_count) return {View::Dashboard, 0};
  if (page_index + 1 < page_count) return {View::Deck, page_index + 1};
  return {View::Dashboard, 0};
}

inline NavigationState navigation_after_previous_press(
    View view, size_t page_index, size_t page_count) {
  if (page_count == 0) return {View::Dashboard, 0};
  if (view == View::Dashboard) return {View::Deck, page_count - 1};
  if (page_index == 0 || page_index >= page_count) {
    return {View::Dashboard, 0};
  }
  return {View::Deck, page_index - 1};
}

}  // namespace rcpaper
