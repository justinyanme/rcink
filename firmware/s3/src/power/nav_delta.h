// src/power/nav_delta.h
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rcpaper {

// Apply a signed delta to a page index with sign-safe modulo wrap.
// `n` must be >= 1 (callers guard against empty page lists upstream).
inline uint8_t apply_nav_delta(uint8_t current, int delta, size_t n) {
  if (n == 0) return 0;
  // Promote to long long so big deltas don't overflow before modulo.
  long long wide = (long long)current + (long long)delta;
  long long m = (long long)n;
  long long r = wide % m;
  if (r < 0) r += m;
  return (uint8_t)r;
}

}  // namespace rcpaper
