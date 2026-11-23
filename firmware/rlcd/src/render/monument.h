#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "proto/envelope.h"
#include "render/token.h"
#include "store/nvs.h"

namespace rcpaper {

constexpr size_t kMonumentDays = 365;
constexpr size_t kMonumentMaximumBytes = 24576;
constexpr const char* kMonumentPageId = "year-monument";

struct MonumentSnapshot {
  std::string start_date;
  std::string end_date;
  // Heap-backed so ESP32-S3 tasks don't put 2.9 KB per copy onto loopTask stack.
  std::vector<uint64_t> days = std::vector<uint64_t>(kMonumentDays, 0);
};

bool decode_monument(const std::string& json, MonumentSnapshot& out,
                     std::string* reason = nullptr);
std::string encode_monument(const MonumentSnapshot& snapshot);
bool monument_content_equal(const MonumentSnapshot& a,
                            const MonumentSnapshot& b);

}  // namespace rcpaper
