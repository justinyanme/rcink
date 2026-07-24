#pragma once

#include <array>
#include <cstdint>

#include "display/pins_config.h"

namespace rcpaper {

struct GpioHold {
  uint8_t pin;
  uint8_t level;
};

enum class PanelStartup : uint8_t {
  Cold,
  Resume,
};

constexpr PanelStartup panel_startup_for_deep_sleep_wake(bool deep_sleep_wake) {
  return deep_sleep_wake ? PanelStartup::Resume : PanelStartup::Cold;
}

constexpr bool panel_startup_uses_hardware_reset(PanelStartup startup) {
  return startup == PanelStartup::Cold;
}

constexpr bool panel_startup_pushes_blank_frame(PanelStartup startup) {
  return startup == PanelStartup::Cold;
}

// Commands sent after the final frame and immediately before SoC deep sleep.
constexpr std::array<uint8_t, 2> kPreDeepSleepCommands = {
    0x39,  // low-power mode
    0x29,  // display on
};

// GPIO levels latched while the digital GPIO peripheral is powered down.
constexpr std::array<GpioHold, 5> kPreDeepSleepGpioHolds = {{
    {ST7305_RST, 1},  // reset deasserted
    {ST7305_CS, 1},   // ignore SPI inputs
    {ST7305_SCK, 0},  // SPI mode-0 idle
    {ST7305_MOSI, 0},
    {ST7305_DC, 0},
}};

}  // namespace rcpaper
