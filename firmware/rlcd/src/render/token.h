#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "proto/envelope.h"
#include "render/link_status.h"
#include "store/nvs.h"

namespace rcpaper {

constexpr size_t kTokenDays = 30;
constexpr size_t kTokenMaxProviders = 4;
constexpr size_t kTokenMaximumBytes = 8192;

struct TokenProvider {
  std::string id;
  std::array<uint64_t, kTokenDays> days;
  uint64_t lifetime = 0;
};

struct TokenSnapshot {
  std::array<TokenProvider, kTokenMaxProviders> providers;
  size_t count = 0;
  uint64_t served_at = 0;
  uint64_t required_source_collected_at = 0;  // 0 represents JSON null.
  std::string today;
  std::optional<uint64_t> total_lifetime;  // Includes providers without active rows (v3).
};

// Independent of Allowance sync state. `{}` means no Token sync has completed.
struct TokenSync {
  bool fetch_failed;
  uint64_t last_ok_s;
  uint64_t retry_at_s;
  bool syncing;
  LinkFailure link_failure;
  uint32_t retry_cooldown_s;
  const char* wifi_ssid;
};

constexpr const char* kTokenPageId = "token-usage";

std::string token_provider_name(const std::string& id);
bool decode_token(const std::string& json, TokenSnapshot& out,
                  std::string* reason = nullptr);
std::string encode_token(const TokenSnapshot& snapshot);
bool token_content_equal(const TokenSnapshot& a, const TokenSnapshot& b);
PageRecord token_page(const TokenSnapshot& snapshot, uint64_t now, const TokenSync& sync);
bool update_token_page(DeviceState& device, const TokenSnapshot& snapshot,
                       uint64_t now, const TokenSync& sync);

}  // namespace rcpaper
