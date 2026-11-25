#include <ArduinoJson.h>
#include <unity.h>

#include <cstdlib>
#include <ctime>
#include <string>

#include "render/station_view.h"

using namespace rcpaper;

namespace {

uint64_t local_epoch(int year, int month, int day, int hour, int minute) {
  std::tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_isdst = -1;
  return (uint64_t)mktime(&t);
}

MonumentSnapshot sample_monument() {
  MonumentSnapshot m;
  m.start_date = "2025-11-25";
  m.end_date = "2026-11-24";
  m.days.assign(kMonumentDays, 0);
  for (size_t i = kMonumentDays - 42; i < kMonumentDays - 1; i++) {
    m.days[i] = 10000000ULL + (i - (kMonumentDays - 42)) * 1000000ULL;
  }
  m.days[kMonumentDays - 1] = 12000000ULL;
  return m;
}

}  // namespace

void test_three_provider_daily_setup_kimi_asymmetry_and_codec_round_trip() {
  const uint64_t now = local_epoch(2026, 11, 24, 16, 25);
  UsageSnapshot usage;
  usage.count = 3;
  usage.served_at = now - 60;
  // Intentionally unsorted upstream order: kimi, gemini, codex.
  usage.providers[0] = {"kimi", UsageStatus::Ok, 8.0, now - 60, now + 540,
                        now + 5 * 86400 + 14 * 3600};
  usage.providers[1] = {"gemini", UsageStatus::Ok, 16.0, now - 60, now + 540,
                        now + 5 * 86400 + 3600};
  usage.providers[2] = {"codex", UsageStatus::Ok, 59.0, now - 60, now + 540,
                        now + 2 * 86400 + 16 * 3600};

  TokenSnapshot tok;
  tok.count = 2;
  tok.today = "2026-11-24";
  tok.required_source_collected_at = now - 300;
  tok.providers[0].id = "codex";
  tok.providers[0].days.fill(0);
  tok.providers[0].days[28] = 80000000ULL;
  tok.providers[0].days[29] = 9500000ULL;
  tok.providers[0].lifetime = 820000000000ULL;
  tok.providers[1].id = "gemini";
  tok.providers[1].days.fill(0);
  tok.providers[1].days[28] = 55200000ULL;
  tok.providers[1].days[29] = 8900000ULL;
  tok.providers[1].lifetime = 600000000000ULL;

  const MonumentSnapshot mon = sample_monument();
  const UsageSync u_sync = {false, now - 60, 0, false, LinkFailure::None, 0,
                            nullptr};
  const TokenSync t_sync = {false, now - 180, 0, false, LinkFailure::None, 0,
                            nullptr};

  const StationView v = derive_station_view(
      usage, u_sync, tok, mon, t_sync, now, RuntimeMode::Host,
      HeatmapWindow::Year);

  TEST_ASSERT_EQUAL_STRING("TUE \xC2\xB7 NOV 24 \xC2\xB7 W48",
                           v.header_left.c_str());
  TEST_ASSERT_FALSE(v.eco_last_sync);
  TEST_ASSERT_EQUAL_STRING("16:25", v.clock_hhmm.c_str());
  TEST_ASSERT_EQUAL_STRING("1.42T", v.total_odo.c_str());
  // Live Token today sum = 9.5M + 8.9M = 18.4M > Monument today 12M.
  TEST_ASSERT_EQUAL_STRING("18.4M", v.today_total.c_str());
  TEST_ASSERT_EQUAL_STRING("42d", v.streak_label.c_str());
  TEST_ASSERT_EQUAL_STRING("1Y", v.window_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(53, v.cols);

  // Canonical display order: codex, gemini, kimi.
  TEST_ASSERT_EQUAL_UINT(3, v.row_count);
  TEST_ASSERT_EQUAL_STRING("codex", v.rows[0].id.c_str());
  TEST_ASSERT_EQUAL_UINT8(41, v.rows[0].left);
  TEST_ASSERT_FALSE(v.rows[0].low_fuel);
  TEST_ASSERT_TRUE(v.rows[0].has_token);
  TEST_ASSERT_EQUAL_STRING("89.5M", v.rows[0].seven_day.c_str());

  TEST_ASSERT_EQUAL_STRING("gemini", v.rows[1].id.c_str());
  TEST_ASSERT_EQUAL_UINT8(84, v.rows[1].left);
  TEST_ASSERT_TRUE(v.rows[1].has_token);
  TEST_ASSERT_EQUAL_STRING("64.1M", v.rows[1].seven_day.c_str());

  TEST_ASSERT_EQUAL_STRING("kimi", v.rows[2].id.c_str());
  TEST_ASSERT_EQUAL_UINT8(92, v.rows[2].left);
  TEST_ASSERT_FALSE(v.rows[2].has_token);
  TEST_ASSERT_TRUE(v.rows[2].seven_day.empty());

  // Conservative Synced timestamp uses min(u_sync=16:24, t_sync=16:22) = 16:22.
  TEST_ASSERT_TRUE(v.footer_chip.empty());
  TEST_ASSERT_EQUAL_STRING("Synced 16:22", v.footer_text.c_str());

  // Encode/decode round-trip equality.
  const std::string json = encode_station_view(v);
  TEST_ASSERT_TRUE(json.find("\"rcink.station-view.v1\"") != std::string::npos);
  StationView decoded;
  TEST_ASSERT_TRUE(decode_station_view(json, decoded));
  TEST_ASSERT_TRUE(decoded == v);
}

void test_low_fuel_pre_token_sync_eco_last_sync_and_windows() {
  const uint64_t now = local_epoch(2026, 11, 24, 16, 25);
  UsageSnapshot usage;
  usage.count = 4;
  usage.providers[0] = {"claude", UsageStatus::Ok, 92.0, now - 30, now + 500,
                        now + 36 * 3600};
  usage.providers[1] = {"codex", UsageStatus::Stale, 46.0, now - 3 * 3600,
                        now - 3000, now + 4 * 86400};
  usage.providers[2] = {"gemini", UsageStatus::Unavailable, 0.0, 0, 0, 0};
  usage.providers[3] = {"kimi", UsageStatus::Ok, 10.0, now - 30, now + 500,
                        now + 5 * 86400};

  const TokenSnapshot empty_tok{};
  const MonumentSnapshot mon = sample_monument();
  const UsageSync u_sync = {false, local_epoch(2026, 11, 24, 15, 40), 0, false,
                            LinkFailure::None, 0, nullptr};
  const TokenSync t_sync = {false, 0, 0, false, LinkFailure::None, 0, nullptr};

  const StationView eco_3m = derive_station_view(
      usage, u_sync, empty_tok, mon, t_sync, now, RuntimeMode::Eco,
      HeatmapWindow::ThreeMonths);

  TEST_ASSERT_TRUE(eco_3m.eco_last_sync);
  TEST_ASSERT_EQUAL_STRING("15:40", eco_3m.clock_hhmm.c_str());
  TEST_ASSERT_EQUAL_STRING("3M", eco_3m.window_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(14, eco_3m.cols);
  // Streak stays full-year invariant (42d) even in 3M window.
  TEST_ASSERT_EQUAL_STRING("42d", eco_3m.streak_label.c_str());

  // Claude at 8% live triggers low_fuel; before first Token sync shows "--".
  TEST_ASSERT_EQUAL_UINT8(8, eco_3m.rows[0].left);
  TEST_ASSERT_TRUE(eco_3m.rows[0].low_fuel);
  TEST_ASSERT_TRUE(eco_3m.rows[0].has_token);
  TEST_ASSERT_EQUAL_STRING("--", eco_3m.rows[0].seven_day.c_str());

  // Stale Codex gets its individual age chip ("3h old"), not low_fuel.
  TEST_ASSERT_EQUAL(UsageRowState::Stale, eco_3m.rows[1].state);
  TEST_ASSERT_EQUAL_STRING("3h old", eco_3m.rows[1].chip.c_str());

  // Unavailable Gemini has car_px == -1, countdown == "--", seven_day == "--".
  TEST_ASSERT_EQUAL(UsageRowState::Unavailable, eco_3m.rows[2].state);
  TEST_ASSERT_EQUAL(-1, eco_3m.rows[2].car_px);
  TEST_ASSERT_EQUAL_STRING("--", eco_3m.rows[2].seven_day.c_str());

  // Kimi at 90% has no token column.
  TEST_ASSERT_FALSE(eco_3m.rows[3].has_token);

  StationView restored;
  TEST_ASSERT_TRUE(decode_station_view(encode_station_view(eco_3m), restored));
  TEST_ASSERT_TRUE(restored == eco_3m);

  // Page deduplication across wakes & window changes.
  DeviceState device;
  TEST_ASSERT_TRUE(update_station_page(device, usage, u_sync, empty_tok, mon,
                                       t_sync, now, RuntimeMode::Eco,
                                       HeatmapWindow::ThreeMonths));
  TEST_ASSERT_FALSE(update_station_page(device, usage, u_sync, empty_tok, mon,
                                        t_sync, now, RuntimeMode::Eco,
                                        HeatmapWindow::ThreeMonths));
  TEST_ASSERT_TRUE(update_station_page(device, usage, u_sync, empty_tok, mon,
                                       t_sync, now, RuntimeMode::Eco,
                                       HeatmapWindow::SixMonths));
}

void test_dual_source_footer_precedence_matrix() {
  const uint64_t now = local_epoch(2026, 11, 24, 16, 0);
  UsageSnapshot usage;
  usage.count = 1;
  usage.providers[0] = {"codex", UsageStatus::Ok, 20.0, now - 60, now + 540,
                        now + 86400};
  TokenSnapshot tok;
  tok.count = 1;
  tok.today = "2026-11-24";
  tok.required_source_collected_at = now - 300;
  tok.providers[0] = {"codex", {}, 1000ULL};
  const MonumentSnapshot mon = sample_monument();

  const UsageSync u_ok = {false, now - 60, 0, false, LinkFailure::None, 0,
                          "desk"};
  const TokenSync t_ok = {false, now - 60, 0, false, LinkFailure::None, 0,
                          "desk"};

  // 1. Syncing beats failures.
  UsageSync u_syncing = u_ok;
  u_syncing.syncing = true;
  u_syncing.fetch_failed = true;
  auto v = derive_station_view(usage, u_syncing, tok, mon, t_ok, now,
                               RuntimeMode::Host);
  TEST_ASSERT_EQUAL_STRING("SYNCING", v.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("fetching now", v.footer_text.c_str());

  // 2. Link failures beat fetch failures and unset clock.
  UsageSync u_wifi = {true, 0, now + 60, false, LinkFailure::WifiFailed, 60,
                      "desk"};
  v = derive_station_view(usage, u_wifi, tok, mon, t_ok, 0, RuntimeMode::Host);
  TEST_ASSERT_EQUAL_STRING("NO WIFI", v.footer_chip.c_str());

  // 3. Unset clock when no link failure.
  v = derive_station_view(usage, u_ok, tok, mon, t_ok, 0, RuntimeMode::Host);
  TEST_ASSERT_EQUAL_STRING("NO CLOCK", v.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("--:--", v.clock_hhmm.c_str());

  // 4. Both failed -> SYNC FAILED; single failed -> ALLOWANCE or TOKEN FAIL.
  const UsageSync u_fail = {true, now - 600, now + 60, false, LinkFailure::None,
                            60, "desk"};
  const TokenSync t_fail = {true, now - 600, now + 300, false,
                            LinkFailure::None, 300, "desk"};
  v = derive_station_view(usage, u_fail, tok, mon, t_fail, now,
                          RuntimeMode::Host);
  TEST_ASSERT_EQUAL_STRING("SYNC FAILED", v.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("retry 16:01", v.footer_text.c_str());

  v = derive_station_view(usage, u_fail, tok, mon, t_ok, now,
                          RuntimeMode::Host);
  TEST_ASSERT_EQUAL_STRING("ALLOWANCE", v.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("retry 16:01", v.footer_text.c_str());

  v = derive_station_view(usage, u_ok, tok, mon, t_fail, now,
                          RuntimeMode::Host);
  TEST_ASSERT_EQUAL_STRING("TOKEN FAIL", v.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("retry 16:05", v.footer_text.c_str());

  // 5. Old data: Token M4 > 2h vs Allowance all-stale.
  TokenSnapshot tok_old = tok;
  tok_old.required_source_collected_at = local_epoch(2026, 11, 24, 11, 20);
  v = derive_station_view(usage, u_ok, tok_old, mon, t_ok, now,
                          RuntimeMode::Host);
  TEST_ASSERT_EQUAL_STRING("OLD DATA", v.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("M4 11:20", v.footer_text.c_str());

  UsageSnapshot usage_old = usage;
  usage_old.providers[0].status = UsageStatus::Stale;
  v = derive_station_view(usage_old, u_ok, tok, mon, t_ok, now,
                          RuntimeMode::Host);
  TEST_ASSERT_EQUAL_STRING("OLD DATA", v.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("checked 15:59", v.footer_text.c_str());
}

int main() {
  setenv("TZ", "SGT-8", 1);
  tzset();
  UNITY_BEGIN();
  RUN_TEST(test_three_provider_daily_setup_kimi_asymmetry_and_codec_round_trip);
  RUN_TEST(test_low_fuel_pre_token_sync_eco_last_sync_and_windows);
  RUN_TEST(test_dual_source_footer_precedence_matrix);
  return UNITY_END();
}
