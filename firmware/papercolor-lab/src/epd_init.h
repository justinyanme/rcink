// Pinned native controller sequences, copied from M5GFX 5268353
// src/lgfx/v1/panel/Panel_ED2208.{hpp,cpp}. Format: cmd, length, data...
// The host test compares these tables against a second copy of the source.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rclab {

// Panel_ED2208::getInitCommands list0 followed by the 0x61 resolution command
// that _init_sequence() sends for a 400x600 panel (01 90 02 58).
constexpr uint8_t kNativeInit[] = {
    0xAA, 6, 0x49, 0x55, 0x20, 0x08, 0x09, 0x18,  // CMDH
    0x01, 1, 0x3F,
    0x00, 2, 0x5F, 0x69,                          // PSR
    0x05, 4, 0x40, 0x1F, 0x1F, 0x2C,
    0x08, 4, 0x6F, 0x1F, 0x1F, 0x22,
    0x06, 4, 0x6F, 0x1F, 0x17, 0x17,              // booster, pre power-on
    0x03, 4, 0x03, 0x54, 0x00, 0x44,
    0x60, 2, 0x02, 0x00,
    0x30, 1, 0x08,
    0x50, 1, 0x3F,                                // CDI
    0xE3, 1, 0x2F,
    0x84, 1, 0x01,
    0x61, 4, 0x01, 0x90, 0x02, 0x58,              // resolution 400x600
};

// Panel_ED2208::_turn_on_display: booster bytes sent after 0x04 power on.
constexpr uint8_t kBoosterAfterPowerOn[] = {0x6F, 0x1F, 0x17, 0x27};

constexpr uint8_t kCmdPowerOn = 0x04;
constexpr uint8_t kCmdPowerOff = 0x02;
constexpr uint8_t kCmdFrame = 0x10;
constexpr uint8_t kCmdRefresh = 0x12;
constexpr uint8_t kCmdLut = 0x20;
constexpr uint8_t kCmdBooster = 0x06;
constexpr size_t kLutBytes = 799;

}  // namespace rclab
