#pragma once

#include <stdint.h>

namespace rcpaper {

enum class WakeReason : uint8_t {
  PowerOn = 0,
  Timer,
  ButtonA,
  ButtonB,
  ButtonC,
  ButtonAB,
  Unknown,
};

constexpr uint8_t kButtonAGpio = 9;
constexpr uint8_t kButtonBGpio = 10;
constexpr uint8_t kButtonCGpio = 1;

constexpr uint64_t kButtonAWakeMask = 1ULL << kButtonAGpio;
constexpr uint64_t kButtonBWakeMask = 1ULL << kButtonBGpio;
constexpr uint64_t kButtonCWakeMask = 1ULL << kButtonCGpio;
constexpr uint64_t kButtonWakeMask =
    kButtonAWakeMask | kButtonBWakeMask | kButtonCWakeMask;

inline WakeReason wake_reason_from_ext1_mask(uint64_t mask) {
  if ((mask & (kButtonAWakeMask | kButtonBWakeMask)) ==
      (kButtonAWakeMask | kButtonBWakeMask)) {
    return WakeReason::ButtonAB;
  }
  if (mask & kButtonAWakeMask) return WakeReason::ButtonA;
  if (mask & kButtonBWakeMask) return WakeReason::ButtonB;
  if (mask & kButtonCWakeMask) return WakeReason::ButtonC;
  return WakeReason::Unknown;
}

}  // namespace rcpaper
