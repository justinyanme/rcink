#pragma once

#include <tuple>

#include "render/station_view.h"
#include "sense/battery.h"
#include "sense/shtc3.h"

namespace rcpaper {

constexpr const char* kNeonPageId = "neon-alley";

struct NeonProvider {
  char id_initial;
  int left;  // -1 = unavailable (JSON null)
  bool low_fuel;
  bool operator==(const NeonProvider& other) const {
    return id_initial == other.id_initial && left == other.left && low_fuel == other.low_fuel;
  }
};

struct NeonView {
  std::string clock_hhmm = "--:--";
  std::string weekday;
  uint8_t day = 0;  // no calendar date until the RTC is valid
  std::string env_temp = "--";
  std::vector<NeonProvider> providers;
  std::array<uint8_t, kSkylineMaxBars> skyline_bars{};
  std::string total_odo = "--";
  uint8_t battery_pct = 0;
  bool battery_low = false;
  bool outage = false;
  std::string footer_chip, footer_text;
  bool cat_enabled = true;

  bool operator==(const NeonView& other) const {
    auto fields = [](const NeonView& v) {
      return std::tie(v.clock_hhmm, v.weekday, v.day, v.env_temp, v.providers,
                      v.skyline_bars, v.total_odo, v.battery_pct, v.battery_low,
                      v.outage, v.footer_chip, v.footer_text, v.cat_enabled);
    };
    return fields(*this) == fields(other);
  }
};

NeonView derive_neon_view(const UsageSnapshot& usage, UsageSync usage_sync,
                          const TokenSnapshot& token, const MonumentSnapshot& monument,
                          TokenSync token_sync, MonumentRetry retry, uint64_t now,
                          const Shtc3Reading& env, const BatteryReading& battery,
                          bool cat_enabled);
std::string encode_neon_view(const NeonView& view);
bool decode_neon_view(const std::string& json, NeonView& out);
bool update_neon_page(PageRecord& page, const NeonView& view, uint64_t now);

}  // namespace rcpaper
