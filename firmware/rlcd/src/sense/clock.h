#pragma once
#include <cstdint>

namespace rcpaper {

bool wall_clock_is_valid();
constexpr bool wall_clock_is_valid(int64_t epoch) {
  return epoch >= 1704067200;  // 2024-01-01 UTC
}
void seed_clock_from_rtc();
void write_rtc_from_clock();

}  // namespace rcpaper
