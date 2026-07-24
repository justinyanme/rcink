#pragma once

#include <cstdint>

namespace rcpaper {

enum class PanelStartup : uint8_t {
  Cold,
  Resume,
};

enum class FirmwareMode : uint8_t {
  Development,
  Production,
};

constexpr bool clear_panel_on_startup(PanelStartup startup) {
  return startup == PanelStartup::Cold;
}

constexpr bool render_fetch_status_on_panel(FirmwareMode mode) {
  return mode == FirmwareMode::Development;
}

}  // namespace rcpaper
