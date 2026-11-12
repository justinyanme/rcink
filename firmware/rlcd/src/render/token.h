#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "proto/envelope.h"
#include "store/nvs.h"

namespace rcpaper {

constexpr size_t kTokenDays = 30;
constexpr size_t kTokenProviderCount = 3;
constexpr size_t kTokenMaximumBytes = 8192;

struct TokenProvider {
  std::array<uint64_t, kTokenDays> days;
  uint64_t lifetime = 0;
};

struct TokenSnapshot {
  std::array<TokenProvider, kTokenProviderCount> providers;
  uint64_t served_at = 0;
  uint64_t required_source_collected_at = 0;  // 0 represents JSON null.
  std::string today;
};

// Independent of Allowance sync state. `{}` means no Token sync has completed.
struct TokenSync {
  bool fetch_failed;
  uint64_t last_ok_s;
  uint64_t retry_at_s;
  bool syncing;
};

constexpr const char* kTokenPageId = "token-usage";

bool decode_token(const std::string& json, TokenSnapshot& out);
std::string encode_token(const TokenSnapshot& snapshot);
PageRecord token_page(const TokenSnapshot& snapshot, uint64_t now, const TokenSync& sync);
bool update_token_page(DeviceState& device, const TokenSnapshot& snapshot,
                       uint64_t now, const TokenSync& sync);

}  // namespace rcpaper
