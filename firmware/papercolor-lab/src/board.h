// PaperColor board bring-up: PM1 power manager over I2C, chip-select idle levels.
// Values copied from M5GFX 5268353 src/M5GFX.cpp autodetect, board_M5PaperColor.
#pragma once

#include <cstdint>

namespace rclab::board {

constexpr int kI2cSda = 3;
constexpr int kI2cScl = 2;
constexpr uint8_t kPm1Addr = 0x6E;
constexpr int kPinEpdCs = 44;
constexpr int kPinSdCs = 47;

// Fatal (abort with a "[boot] fatal:" line) when PM1 is absent or any register
// transaction fails. Returns the PM1 supply register 0x04 raw value.
uint8_t begin();

}  // namespace rclab::board
