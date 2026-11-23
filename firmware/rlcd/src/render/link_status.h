#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace rcpaper {

enum class LinkFailure : uint8_t { None = 0, WifiFailed = 1, NtpFailed = 2 };

// Exact xAdvance widths (0x20..0x7E) from FreeSans9pt7b.h.
constexpr uint8_t kFreeSans9Advance[95] = {
    5,  6,  6, 10, 10, 16, 12,  4,  6,  6,  7, 11,  5,  6,  5,  5,
   10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  5,  5, 11, 11, 11, 10,
   18, 12, 12, 13, 13, 11, 11, 14, 13,  5, 10, 12, 10, 15, 13, 14,
   12, 14, 13, 12, 11, 13, 12, 17, 12, 12, 11,  5,  5,  5,  8, 10,
    5, 10, 10,  9, 10, 10,  5, 10, 10,  4,  4,  9,  4, 15, 10, 10,
   10, 10,  6,  9,  5, 10,  9, 13,  9,  9,  9,  6,  4,  6,  9,
};

constexpr uint16_t kMaxWifiHeadlinePx = 264;  // Token headline budget: 280 - 12 - 4
constexpr uint16_t kWifiPrefixPx = 80;        // freesans9_width("No Wi-Fi: ")
constexpr uint16_t kEllipsisPx = 15;          // freesans9_width("...")
constexpr size_t kMaxWifiSsidChars = 28;      // "No Wi-Fi: " (10) + 28 = 38 <= kMaxLabel (40)

inline uint16_t freesans9_width(const std::string& text) {
  uint16_t width = 0;
  for (unsigned char c : text) {
    if (c >= 0x20 && c <= 0x7E) width += kFreeSans9Advance[c - 0x20];
  }
  return width;
}

inline std::string sanitize_wifi_ssid(const char* raw) {
  if (raw == nullptr) return "";
  std::string clean;
  for (const unsigned char* p = (const unsigned char*)raw; *p != '\0'; ++p) {
    if (*p >= 0x20 && *p <= 0x7E) clean.push_back((char)*p);
  }
  const size_t first = clean.find_first_not_of(' ');
  if (first == std::string::npos) return "";
  const size_t last = clean.find_last_not_of(' ');
  clean = clean.substr(first, last - first + 1);

  if (clean.size() <= kMaxWifiSsidChars &&
      freesans9_width(clean) <= kMaxWifiHeadlinePx - kWifiPrefixPx) {
    return clean;
  }

  const uint16_t budget = kMaxWifiHeadlinePx - kWifiPrefixPx - kEllipsisPx;
  std::string prefix;
  uint16_t prefix_px = 0;
  for (unsigned char c : clean) {
    const uint8_t advance = kFreeSans9Advance[c - 0x20];
    if (prefix.size() >= kMaxWifiSsidChars - 3 || prefix_px + advance > budget) {
      break;
    }
    prefix.push_back((char)c);
    prefix_px += advance;
  }
  while (!prefix.empty() && prefix.back() == ' ') prefix.pop_back();
  return prefix + "...";
}

inline std::string wifi_failed_headline(const char* ssid) {
  const std::string clean = sanitize_wifi_ssid(ssid);
  return clean.empty() ? "No Wi-Fi." : "No Wi-Fi: " + clean;
}

inline const char* ntp_failed_headline() {
  return "NTP timed out. Clock not set.";
}

inline std::string format_local_retry_hhmm(uint64_t epoch_s) {
  const time_t instant = (time_t)epoch_s;
  std::tm local = {};
  localtime_r(&instant, &local);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "retry %02d:%02d", local.tm_hour, local.tm_min);
  return buf;
}

inline std::string retry_footer_text(uint64_t now, bool clock_valid,
                                     uint64_t retry_at_s,
                                     uint32_t retry_cooldown_s) {
  if (clock_valid) {
    return retry_at_s > now ? format_local_retry_hhmm(retry_at_s) : "retrying";
  }
  if (retry_at_s == 0 || now >= retry_at_s || retry_cooldown_s == 0) {
    return "retrying";
  }
  if (retry_cooldown_s >= 3600) {
    const unsigned long hours = (retry_cooldown_s + 1800UL) / 3600UL;
    return "retry ~" + std::to_string(hours) + "h";
  }
  unsigned long mins = (retry_cooldown_s + 30UL) / 60UL;
  if (mins < 1) mins = 1;
  return "retry ~" + std::to_string(mins) + "m";
}

}  // namespace rcpaper
