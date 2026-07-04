#pragma once
#include <cstdint>

namespace rcpaper {

[[noreturn]] void deep_sleep_until(uint64_t epoch_seconds);

}  // namespace rcpaper
