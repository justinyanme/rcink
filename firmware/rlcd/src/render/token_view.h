#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "render/token.h"

namespace rcpaper {

constexpr uint8_t kTokenChartPx = 30;

struct TokenRowView {
  std::string id;
  std::array<uint64_t, kTokenDays> shifted_days;
  std::array<uint8_t, kTokenDays> bars;
  uint8_t usual_px = 0;
  uint64_t lifetime_value = 0;
  std::string seven_day;
  std::string today;
  std::string lifetime;
};

struct TokenView {
  std::array<TokenRowView, kTokenMaxProviders> rows;
  size_t count = 0;
  std::string headline;
  int8_t headline_pick = -1;
  std::string footer_chip;
  std::string footer_text;
};

inline bool operator==(const TokenRowView& a, const TokenRowView& b) {
  return a.id == b.id && a.shifted_days == b.shifted_days && a.bars == b.bars &&
         a.usual_px == b.usual_px && a.lifetime_value == b.lifetime_value &&
         a.seven_day == b.seven_day && a.today == b.today && a.lifetime == b.lifetime;
}
inline bool operator==(const TokenView& a, const TokenView& b) {
  if (a.count != b.count || a.headline != b.headline ||
      a.headline_pick != b.headline_pick || a.footer_chip != b.footer_chip ||
      a.footer_text != b.footer_text) return false;
  for (size_t i = 0; i < a.count; i++) {
    if (!(a.rows[i] == b.rows[i])) return false;
  }
  return true;
}

std::string format_tokens(uint64_t value);
TokenView derive_token_view(const TokenSnapshot& snapshot, uint64_t now,
                            const TokenSync& sync);
std::string encode_token_view(const TokenView& view);
bool decode_token_view(const std::string& json, TokenView& out);

}  // namespace rcpaper
