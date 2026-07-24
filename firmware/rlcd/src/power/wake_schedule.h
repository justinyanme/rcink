#pragma once

#include <cstdint>

namespace rcpaper {

constexpr uint64_t kHostProbeIntervalSec = 30;

inline uint64_t next_probe_wake_epoch(uint64_t now_s) {
  return (now_s / kHostProbeIntervalSec + 1) * kHostProbeIntervalSec;
}

}  // namespace rcpaper
