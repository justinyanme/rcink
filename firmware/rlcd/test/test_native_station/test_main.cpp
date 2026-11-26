#include <ArduinoJson.h>
#include <unity.h>

#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "render/station_view.h"
#include "../fixtures/token_response.h"

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

void test_three_provider_daily_setup_kimi_presence_fallback_and_codec_round_trip() {
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

  // Default Station window is 6M coupled with 8W skyline.
  const StationView v = derive_station_view(
      usage, u_sync, tok, mon, t_sync, now, RuntimeMode::Host);

  TEST_ASSERT_EQUAL_STRING("STATION", v.header_left.c_str());
  TEST_ASSERT_FALSE(v.eco_last_sync);
  TEST_ASSERT_EQUAL_STRING("16:25", v.clock_hhmm.c_str());
  TEST_ASSERT_EQUAL_UINT16(16 * 60 + 25, v.minutes_of_day);
  TEST_ASSERT_EQUAL_STRING("1.42T", v.total_odo.c_str());
  // Live Token today sum = 9.5M + 8.9M = 18.4M > Monument today 12M.
  TEST_ASSERT_EQUAL_STRING("18.4M", v.today_total.c_str());
  TEST_ASSERT_EQUAL_STRING("42d", v.streak_label.c_str());
  TEST_ASSERT_EQUAL_STRING("6M", v.window_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(27, v.cols);
  TEST_ASSERT_EQUAL_STRING("8W", v.skyline.label.c_str());
  TEST_ASSERT_EQUAL_UINT8(8, v.skyline.bar_count);

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

  // Absent from TokenSnapshot -> Kimi falls back to em-dash (has_token == false).
  TEST_ASSERT_EQUAL_STRING("kimi", v.rows[2].id.c_str());
  TEST_ASSERT_EQUAL_UINT8(92, v.rows[2].left);
  TEST_ASSERT_FALSE(v.rows[2].has_token);
  TEST_ASSERT_TRUE(v.rows[2].seven_day.empty());

  // When Kimi IS present in TokenSnapshot, its 7D total lights up on Station Page.
  TokenSnapshot tok_with_kimi = tok;
  tok_with_kimi.count = 3;
  tok_with_kimi.providers[2].id = "kimi";
  tok_with_kimi.providers[2].days.fill(0);
  tok_with_kimi.providers[2].days[28] = 20000000ULL;
  tok_with_kimi.providers[2].days[29] = 8400000ULL;
  tok_with_kimi.providers[2].lifetime = 90000000ULL;
  const StationView v_kimi = derive_station_view(
      usage, u_sync, tok_with_kimi, mon, t_sync, now, RuntimeMode::Host);
  TEST_ASSERT_TRUE(v_kimi.rows[2].has_token);
  TEST_ASSERT_EQUAL_STRING("28.4M", v_kimi.rows[2].seven_day.c_str());

  // Conservative Synced timestamp uses min(u_sync=16:24, t_sync=16:22) = 16:22.
  TEST_ASSERT_TRUE(v.footer_chip.empty());
  TEST_ASSERT_EQUAL_STRING("Synced 16:22", v.footer_text.c_str());

  // Encode/decode round-trip equality.
  const std::string json = encode_station_view(v_kimi);
  TEST_ASSERT_TRUE(json.find("\"rcink.station-view.v2\"") != std::string::npos);
  StationView decoded;
  TEST_ASSERT_TRUE(decode_station_view(json, decoded));
  TEST_ASSERT_TRUE(decoded == v_kimi);
}

void test_zai_fourth_row_and_retired_usage_in_total_odo_and_heatmap() {
  const uint64_t now = local_epoch(2026, 11, 24, 16, 25);
  TokenSnapshot token;
  TEST_ASSERT_TRUE(decode_token(zai_token_response(), token));
  const TokenSync token_sync = {false, now - 180, 0, false};
  UsageSnapshot usage;
  usage.count = token.count;
  for (size_t i = 0; i < token.count; i++) {
    usage.providers[i] = {token.providers[i].id, UsageStatus::Ok, 10.0,
                          now - 60, now + 540, now + 86400};
  }
  MonumentSnapshot history = sample_monument();
  history.days.assign(kMonumentDays, 0);
  history.days[kMonumentDays - 2] = 80000000ULL;  // In-window retired Kimi usage.

  const StationView station = derive_station_view(
      usage, {}, token, history, token_sync, now, RuntimeMode::Host);
  TEST_ASSERT_EQUAL_UINT(4, station.row_count);
  TEST_ASSERT_EQUAL_STRING("zai", station.rows[3].id.c_str());
  TEST_ASSERT_EQUAL_STRING("28.4M", station.rows[3].seven_day.c_str());
  TEST_ASSERT_EQUAL_UINT64(7570000000ULL, station.total_odo_value);
  TEST_ASSERT_EQUAL_UINT64(101500000ULL, station.skyline.current_sum);
  TEST_ASSERT_EQUAL_STRING("2d", station.streak_label.c_str());

  const MonumentView monument = derive_monument_view(
      history, token, now, token_sync, RuntimeMode::Host, HeatmapWindow::SixMonths);
  TEST_ASSERT_EQUAL_UINT64(station.total_odo_value, monument.total_odo_value);
  TEST_ASSERT_EQUAL_STRING("2d", monument.active_badge.c_str());
  TEST_ASSERT_TRUE(station.cells == monument.cells);

  // The v3 total remains authoritative if the independently fetched heatmap
  // is newer. A v2 payload still uses the annual history floor.
  history.days[kMonumentDays - 1] = 8000000000ULL;
  TEST_ASSERT_EQUAL_UINT64(7570000000ULL, derive_monument_view(
      history, token, now, token_sync, RuntimeMode::Host).total_odo_value);
  token.total_lifetime.reset();
  TEST_ASSERT_EQUAL_UINT64(8080000000ULL, derive_monument_view(
      history, token, now, token_sync, RuntimeMode::Host).total_odo_value);
}

void test_skyline_buckets_deltas_baselines_and_window_coupling() {
  TEST_ASSERT_EQUAL(SkylineScale::EightWeeks,
                    skyline_scale_for_window(HeatmapWindow::SixMonths));
  TEST_ASSERT_EQUAL(SkylineScale::SevenDays,
                    skyline_scale_for_window(HeatmapWindow::ThreeMonths));
  TEST_ASSERT_EQUAL(SkylineScale::SixMonths,
                    skyline_scale_for_window(HeatmapWindow::Year));

  // Synthetic 365-day series:
  // - Days 0..350: 100 per day
  // - Days 351..357 (prior 7d): 100 per day -> sum = 700
  // - Days 358..364 (current 7d): 150 per day -> sum = 1050 (+50% WoW)
  std::vector<uint64_t> days(kMonumentDays, 100ULL);
  for (size_t i = kMonumentDays - 7; i < kMonumentDays; i++) days[i] = 150ULL;

  const TokenSkylineView s7 =
      derive_token_skyline(days, true, SkylineScale::SevenDays);
  TEST_ASSERT_EQUAL_STRING("7D", s7.label.c_str());
  TEST_ASSERT_EQUAL_UINT8(7, s7.bar_count);
  TEST_ASSERT_EQUAL_UINT64(1050ULL, s7.current_sum);
  TEST_ASSERT_EQUAL_UINT64(700ULL, s7.prior_sum);
  TEST_ASSERT_TRUE(s7.has_delta);
  TEST_ASSERT_EQUAL_INT16(50, s7.delta_pct);
  TEST_ASSERT_TRUE(s7.delta_positive);
  TEST_ASSERT_EQUAL_STRING("+50%", s7.delta_label.c_str());
  TEST_ASSERT_GREATER_THAN(0, s7.baseline_px);
  TEST_ASSERT_LESS_OR_EQUAL(kStationSkylineMaxBarH, s7.baseline_px);

  const TokenSkylineView s8 =
      derive_token_skyline(days, true, SkylineScale::EightWeeks);
  TEST_ASSERT_EQUAL_STRING("8W", s8.label.c_str());
  TEST_ASSERT_EQUAL_UINT8(8, s8.bar_count);
  TEST_ASSERT_EQUAL_UINT64(1050ULL, s8.current_sum);
  TEST_ASSERT_EQUAL_UINT64(700ULL, s8.prior_sum);
  TEST_ASSERT_EQUAL_INT16(50, s8.delta_pct);
  TEST_ASSERT_EQUAL_STRING("+50%", s8.delta_label.c_str());

  // 6M monthly: last 30d has 23*100 + 7*150 = 3350 vs prior 30d = 3000 (+12% MoM).
  const TokenSkylineView s6 =
      derive_token_skyline(days, true, SkylineScale::SixMonths);
  TEST_ASSERT_EQUAL_STRING("6M", s6.label.c_str());
  TEST_ASSERT_EQUAL_UINT8(6, s6.bar_count);
  TEST_ASSERT_EQUAL_UINT64(3350ULL, s6.current_sum);
  TEST_ASSERT_EQUAL_UINT64(3000ULL, s6.prior_sum);
  TEST_ASSERT_EQUAL_INT16(12, s6.delta_pct);
  TEST_ASSERT_EQUAL_STRING("+12%", s6.delta_label.c_str());

  // Edge cases: zero prior period -> "NEW"; all-zero -> "0%"; negative -> "-50%".
  std::vector<uint64_t> zero_prior(kMonumentDays, 0ULL);
  zero_prior[kMonumentDays - 1] = 500ULL;
  const TokenSkylineView s_new =
      derive_token_skyline(zero_prior, true, SkylineScale::EightWeeks);
  TEST_ASSERT_TRUE(s_new.has_delta);
  TEST_ASSERT_TRUE(s_new.delta_positive);
  TEST_ASSERT_EQUAL_STRING("NEW", s_new.delta_label.c_str());

  std::vector<uint64_t> all_zero(kMonumentDays, 0ULL);
  const TokenSkylineView s_zero =
      derive_token_skyline(all_zero, true, SkylineScale::EightWeeks);
  TEST_ASSERT_FALSE(s_zero.has_delta);
  TEST_ASSERT_EQUAL_STRING("0%", s_zero.delta_label.c_str());

  std::vector<uint64_t> cooling(kMonumentDays, 0ULL);
  for (size_t i = kMonumentDays - 14; i < kMonumentDays - 7; i++) cooling[i] = 200ULL;
  for (size_t i = kMonumentDays - 7; i < kMonumentDays; i++) cooling[i] = 100ULL;
  const TokenSkylineView s_cool =
      derive_token_skyline(cooling, true, SkylineScale::SevenDays);
  TEST_ASSERT_TRUE(s_cool.has_delta);
  TEST_ASSERT_FALSE(s_cool.delta_positive);
  TEST_ASSERT_EQUAL_INT16(-50, s_cool.delta_pct);
  TEST_ASSERT_EQUAL_STRING("-50%", s_cool.delta_label.c_str());
}

void test_sky_phases_celestial_arc_bounds_and_static_eco_determinism() {
  TEST_ASSERT_EQUAL(SkyPhase::Night, sky_phase_for_minutes(0));
  TEST_ASSERT_EQUAL(SkyPhase::Night, sky_phase_for_minutes(5 * 60 + 29));
  TEST_ASSERT_EQUAL(SkyPhase::Dawn, sky_phase_for_minutes(5 * 60 + 30));
  TEST_ASSERT_EQUAL(SkyPhase::Dawn, sky_phase_for_minutes(7 * 60 + 29));
  TEST_ASSERT_EQUAL(SkyPhase::Day, sky_phase_for_minutes(7 * 60 + 30));
  TEST_ASSERT_EQUAL(SkyPhase::Day, sky_phase_for_minutes(16 * 60 + 59));
  TEST_ASSERT_EQUAL(SkyPhase::Dusk, sky_phase_for_minutes(17 * 60 + 0));
  TEST_ASSERT_EQUAL(SkyPhase::Dusk, sky_phase_for_minutes(19 * 60 + 29));
  TEST_ASSERT_EQUAL(SkyPhase::Night, sky_phase_for_minutes(19 * 60 + 30));
  TEST_ASSERT_EQUAL(SkyPhase::Night, sky_phase_for_minutes(23 * 60 + 59));

  // Verify sun/moon arc stays strictly within the 112 px skyline box for every minute.
  for (uint16_t m = 0; m < 1440; m++) {
    const SkyVisualState s = derive_sky_visual(m, true, m);
    const int16_t body_w = s.is_moon ? 6 : 7;
    const int16_t body_h = s.is_moon ? 6 : 7;
    TEST_ASSERT_GREATER_OR_EQUAL(kStationSkylineX, s.body_x);
    TEST_ASSERT_LESS_OR_EQUAL(kStationSkylineRight, s.body_x + body_w);
    TEST_ASSERT_GREATER_OR_EQUAL(kStationSkylineTop, s.body_y);
    TEST_ASSERT_LESS_OR_EQUAL(kStationSkylineGroundY, s.body_y + body_h);
  }

  // Noon sun should sit higher (smaller y) than sunrise/sunset sun.
  const SkyVisualState dawn = derive_sky_visual(5 * 60 + 30, false, 0);
  const SkyVisualState noon = derive_sky_visual(12 * 60 + 30, false, 0);
  TEST_ASSERT_LESS_THAN(dawn.body_y, noon.body_y);

  // Static ACTIVE/ECO output (animating == false) is 100% invariant across tick values.
  const SkyVisualState eco_day_a = derive_sky_visual(12 * 60, false, 0);
  const SkyVisualState eco_day_b = derive_sky_visual(12 * 60, false, 999);
  TEST_ASSERT_TRUE(eco_day_a == eco_day_b);
  TEST_ASSERT_TRUE(eco_day_a.beacon_on);

  const SkyVisualState eco_night_a = derive_sky_visual(23 * 60, false, 0);
  const SkyVisualState eco_night_b = derive_sky_visual(23 * 60, false, 777);
  TEST_ASSERT_TRUE(eco_night_a == eco_night_b);
  TEST_ASSERT_TRUE(eco_night_a.is_moon);
  TEST_ASSERT_TRUE(eco_night_a.beacon_on);
}

void test_low_fuel_pre_token_sync_eco_last_sync_and_windows() {
  const uint64_t now = local_epoch(2026, 11, 24, 16, 25);
  UsageSnapshot usage;
  usage.count = 4;
  usage.providers[0] = {"claude", UsageStatus::Ok, 92.0, now - 30, now + 500,
                        now + 36 * 3600 + 1800};
  usage.providers[1] = {"codex", UsageStatus::Stale, 46.0, now - 3 * 3600,
                        now - 3000, now + 4 * 86400 + 1800};
  usage.providers[2] = {"gemini", UsageStatus::Unavailable, 0.0, 0, 0, 0};
  usage.providers[3] = {"kimi", UsageStatus::Ok, 10.0, now - 30, now + 500,
                        now + 5 * 86400 + 1800};

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
  TEST_ASSERT_EQUAL_UINT16(15 * 60 + 40, eco_3m.minutes_of_day);
  TEST_ASSERT_EQUAL_STRING("3M", eco_3m.window_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(14, eco_3m.cols);
  TEST_ASSERT_EQUAL_STRING("7D", eco_3m.skyline.label.c_str());
  TEST_ASSERT_EQUAL_UINT8(7, eco_3m.skyline.bar_count);
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

  // Kimi at 90% falls back to em-dash when absent from TokenSnapshot.
  TEST_ASSERT_FALSE(eco_3m.rows[3].has_token);

  StationView restored;
  TEST_ASSERT_TRUE(decode_station_view(encode_station_view(eco_3m), restored));
  TEST_ASSERT_TRUE(restored == eco_3m);

  // Page deduplication across wakes (even at later ECO minute!) & window changes.
  DeviceState device;
  TEST_ASSERT_TRUE(update_station_page(device, usage, u_sync, empty_tok, mon,
                                       t_sync, now, RuntimeMode::Eco,
                                       HeatmapWindow::ThreeMonths));
  // 30 s later in ECO, LAST SYNC time is unchanged so layout_json stays identical.
  TEST_ASSERT_FALSE(update_station_page(device, usage, u_sync, empty_tok, mon,
                                        t_sync, now + 30, RuntimeMode::Eco,
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
  RUN_TEST(test_three_provider_daily_setup_kimi_presence_fallback_and_codec_round_trip);
  RUN_TEST(test_zai_fourth_row_and_retired_usage_in_total_odo_and_heatmap);
  RUN_TEST(test_skyline_buckets_deltas_baselines_and_window_coupling);
  RUN_TEST(test_sky_phases_celestial_arc_bounds_and_static_eco_determinism);
  RUN_TEST(test_low_fuel_pre_token_sync_eco_last_sync_and_windows);
  RUN_TEST(test_dual_source_footer_precedence_matrix);
  return UNITY_END();
}
