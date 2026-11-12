#include <ArduinoJson.h>
#include <unity.h>
#include <functional>
#include <cstdlib>
#include <limits>
#include <ctime>
#include <string>

#include "render/token.h"
#include "render/token_view.h"
#include "render/usage.h"
#include "../fixtures/token_response.h"

using namespace rcpaper;

void test_decoder_accepts_exact_schema_order_and_64_bit_totals() {
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  TEST_ASSERT_EQUAL_UINT64(1794446400ULL, snapshot.served_at);
  TEST_ASSERT_EQUAL_UINT64(1794442800ULL, snapshot.required_source_collected_at);
  TEST_ASSERT_EQUAL_STRING("2026-11-12", snapshot.today.c_str());
  TEST_ASSERT_EQUAL_UINT64(5000000000ULL, snapshot.providers[1].days[0]);
  TEST_ASSERT_EQUAL_UINT64(43400000000ULL, snapshot.providers[1].lifetime);
  TEST_ASSERT_EQUAL_UINT64(0ULL, snapshot.providers[2].lifetime);
  for (uint64_t day : snapshot.providers[2].days) TEST_ASSERT_EQUAL_UINT64(0ULL, day);
}

void test_decoder_rejects_malformed_bounded_and_non_integer_responses_without_mutation() {
  TokenSnapshot cache;
  TEST_ASSERT_TRUE(decode_token(token_response(), cache));
  const std::string original = encode_token(cache);
  const std::function<void(JsonDocument&)> mutations[] = {
      [](JsonDocument& d) { d["schema"] = "rcink.token-usage.v2"; },
      [](JsonDocument& d) { d["extra"] = true; },
      [](JsonDocument& d) { d["today"] = "2026-02-30"; },
      [](JsonDocument& d) { d["today"] = "12-11-2026"; },
      [](JsonDocument& d) { d["requiredSourceCollectedAt"] = 0; },
      [](JsonDocument& d) { d["providers"].as<JsonArray>().remove(2); },
      [](JsonDocument& d) { d["providers"][1]["id"] = "claude"; },
      [](JsonDocument& d) { d["providers"][0]["days"].as<JsonArray>().remove(0); },
      [](JsonDocument& d) { d["providers"][0]["days"].as<JsonArray>().add(0); },
      [](JsonDocument& d) { d["providers"][0]["days"][0] = -1; },
      [](JsonDocument& d) { d["providers"][0]["days"][0] = 1.5; },
      [](JsonDocument& d) { d["providers"][0]["days"][0] = "100"; },
      [](JsonDocument& d) { d["providers"][0]["lifetime"] = -1; },
      [](JsonDocument& d) { d["providers"][0]["extra"] = 1; },
  };
  for (const auto& mutate : mutations) {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, token_response()));
    mutate(doc);
    std::string bad;
    serializeJson(doc, bad);
    TEST_ASSERT_FALSE(decode_token(bad, cache));
    TEST_ASSERT_EQUAL_STRING(original.c_str(), encode_token(cache).c_str());
  }
  for (const std::string& bad : {std::string(), std::string("{broken"),
                                 token_response() + "garbage",
                                 std::string(kTokenMaximumBytes + 1, ' ')}) {
    TEST_ASSERT_FALSE(decode_token(bad, cache));
    TEST_ASSERT_EQUAL_STRING(original.c_str(), encode_token(cache).c_str());
  }
}

TokenView view_of(const TokenSnapshot& snapshot, uint64_t now, TokenSync sync = {}) {
  const PageRecord page = token_page(snapshot, now, sync);
  TEST_ASSERT_EQUAL_STRING(kTokenPageId, page.id.c_str());
  TokenView view;
  TEST_ASSERT_TRUE(decode_token_view(page.layout_json, view));
  TEST_ASSERT_TRUE(view == derive_token_view(snapshot, now, sync));
  return view;
}

uint64_t local_epoch(int year, int month, int day, int hour, int minute) {
  std::tm value = {};
  value.tm_year = year - 1900;
  value.tm_mon = month - 1;
  value.tm_mday = day;
  value.tm_hour = hour;
  value.tm_min = minute;
  value.tm_isdst = -1;
  return (uint64_t)mktime(&value);
}

void test_derived_values_include_zero_filled_days_and_all_zero_gemini() {
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  const TokenView view = view_of(snapshot, 1794446400ULL, {false, 1794446400ULL, 0, false});
  TEST_ASSERT_EQUAL_STRING("1.93B", view.rows[0].seven_day.c_str());
  TEST_ASSERT_EQUAL_STRING("3.15B", view.rows[1].seven_day.c_str());
  TEST_ASSERT_EQUAL_STRING("0", view.rows[2].seven_day.c_str());
  TEST_ASSERT_EQUAL_STRING("today 28M · 10% of a usual day", view.rows[0].today.c_str());
  TEST_ASSERT_EQUAL_STRING("today 0 · 0% of a usual day", view.rows[1].today.c_str());
  TEST_ASSERT_EQUAL_STRING("today 0", view.rows[2].today.c_str());
  TEST_ASSERT_EQUAL_STRING("25.9B", view.rows[0].lifetime.c_str());
  TEST_ASSERT_EQUAL_STRING("43.4B", view.rows[1].lifetime.c_str());
  TEST_ASSERT_EQUAL_STRING("0", view.rows[2].lifetime.c_str());
  TEST_ASSERT_EQUAL_STRING("Busiest this week: Codex", view.headline.c_str());
  TEST_ASSERT_EQUAL(1, view.headline_pick);
  TEST_ASSERT_EQUAL(30, view.rows[0].bars.size());
  TEST_ASSERT_EQUAL(3, view.rows[0].bars[29]);  // small today stays hollow, not solid
  TEST_ASSERT_EQUAL(3, view.rows[2].bars[29]);  // even zero today has a hollow outline
  TEST_ASSERT_EQUAL(0, view.rows[2].usual_px);
  TEST_ASSERT_EQUAL_STRING("Synced 09:20", view.footer_text.c_str());
  TEST_ASSERT_TRUE(view.footer_chip.empty());

  TokenSnapshot partial = snapshot;
  for (TokenProvider& provider : partial.providers) {
    provider.days.fill(0);
    provider.lifetime = 0;
  }
  partial.providers[0].days[27] = 70;
  partial.providers[0].days[28] = 30;
  partial.providers[0].days[29] = 7;
  const TokenView fewer = view_of(partial, 1794446400ULL);
  TEST_ASSERT_EQUAL_STRING("107", fewer.rows[0].seven_day.c_str());
  // The seven-day mean includes zero-filled missing days: (70 + 30) / 7.
  TEST_ASSERT_EQUAL_STRING("today 7 · 49% of a usual day", fewer.rows[0].today.c_str());
  partial.providers[0].days[27] = 0;
  partial.providers[0].days[28] = 0;
  TEST_ASSERT_EQUAL_STRING("today 7", view_of(partial, 1794446400ULL).rows[0].today.c_str());
}

void test_headline_thresholds_materiality_and_suppression() {
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  for (TokenProvider& provider : snapshot.providers) provider.days.fill(0);
  for (size_t p = 0; p < 3; p++) {
    for (size_t i = 22; i <= 28; i++) snapshot.providers[p].days[i] = 100;
  }
  snapshot.providers[0].days[29] = 150;
  TEST_ASSERT_EQUAL_STRING("Claude is on a tear today", view_of(snapshot, 1794446400ULL).headline.c_str());

  // Exactly 25% of the combined usual day is material enough.
  for (size_t i = 22; i <= 28; i++) {
    snapshot.providers[0].days[i] = 100;
    snapshot.providers[1].days[i] = 400;
    snapshot.providers[2].days[i] = 100;
  }
  TEST_ASSERT_EQUAL_STRING("Claude is on a tear today", view_of(snapshot, 1794446400ULL).headline.c_str());
  snapshot.providers[1].days[28]++;  // combined usual is now just over 600
  TEST_ASSERT_EQUAL_STRING("Busiest this week: Codex", view_of(snapshot, 1794446400ULL).headline.c_str());

  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  for (TokenProvider& provider : snapshot.providers) provider.days.fill(0);
  for (size_t i = 22; i <= 28; i++) snapshot.providers[0].days[i] = 100;
  snapshot.providers[0].days[29] = 150;
  snapshot.required_source_collected_at = 0;
  TEST_ASSERT_EQUAL_STRING("Busiest this week: Claude", view_of(snapshot, 1794446400ULL).headline.c_str());
  snapshot.required_source_collected_at = 1794442800ULL;
  TEST_ASSERT_EQUAL_STRING("Busiest this week: Claude",
      view_of(snapshot, 1794446400ULL, {true, 0, 0, false}).headline.c_str());

  TokenSnapshot empty;
  TEST_ASSERT_TRUE(decode_token(token_response(), empty));
  for (TokenProvider& provider : empty.providers) provider.days.fill(0);
  TEST_ASSERT_EQUAL_STRING("Waiting for the first numbers.", view_of(empty, 1794446400ULL).headline.c_str());
}

void test_footer_reports_old_failed_and_syncing_independently() {
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  TokenView view = view_of(snapshot, 1794450001ULL, {false, 1794446400ULL, 0, false});
  TEST_ASSERT_EQUAL_STRING("OLD DATA", view.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("M4 08:20", view.footer_text.c_str());
  TEST_ASSERT_EQUAL_STRING("1.93B", view.rows[0].seven_day.c_str());

  view = view_of(snapshot, 1794446400ULL, {true, 1794442800ULL, 1794446460ULL, false});
  TEST_ASSERT_EQUAL_STRING("SYNC FAILED", view.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("retry 09:21", view.footer_text.c_str());
  view = view_of(snapshot, 1794446400ULL, {true, 1794442800ULL, 1794446460ULL, true});
  TEST_ASSERT_EQUAL_STRING("SYNCING", view.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("fetching now", view.footer_text.c_str());
}

void test_local_midnight_shifts_days_and_makes_new_today_zero() {
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  const uint64_t next_day = local_epoch(2026, 11, 13, 0, 1);
  TokenView view = view_of(snapshot, next_day);
  TEST_ASSERT_EQUAL_UINT64(28000000ULL, view.rows[0].shifted_days[28]);
  TEST_ASSERT_EQUAL_UINT64(0ULL, view.rows[0].shifted_days[29]);
  TEST_ASSERT_EQUAL_STRING("1.78B", view.rows[0].seven_day.c_str());
  TEST_ASSERT_EQUAL_STRING("today 0 · 0% of a usual day", view.rows[0].today.c_str());
  TEST_ASSERT_EQUAL_STRING("25.9B", view.rows[0].lifetime.c_str());

  view = view_of(snapshot, local_epoch(2026, 11, 14, 0, 1));
  TEST_ASSERT_EQUAL_STRING("1.38B", view.rows[0].seven_day.c_str());
  TEST_ASSERT_EQUAL_UINT64(0ULL, view.rows[0].shifted_days[29]);
  view = view_of(snapshot, local_epoch(2027, 1, 1, 0, 1));
  TEST_ASSERT_EQUAL_STRING("Waiting for the first numbers.", view.headline.c_str());
  TEST_ASSERT_EQUAL_STRING("0", view.rows[0].seven_day.c_str());
}

void test_extreme_64_bit_today_share_does_not_overflow_to_negative() {
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  for (TokenProvider& provider : snapshot.providers) provider.days.fill(0);
  snapshot.providers[0].days[28] = 1;
  snapshot.providers[0].days[29] = std::numeric_limits<uint64_t>::max();
  const std::string today = view_of(snapshot, 1794446400ULL).rows[0].today;
  TEST_ASSERT_TRUE(today.find("· -") == std::string::npos);
  TEST_ASSERT_LESS_THAN(160, today.size());
}

void test_two_page_updates_preserve_token_selection_and_dedup_static_frames() {
  TokenSnapshot token;
  TEST_ASSERT_TRUE(decode_token(token_response(), token));
  DeviceState device;
  TEST_ASSERT_TRUE(update_usage_page(device, UsageSnapshot{}, 1794446400ULL,
                                     {false, 0, 0, false}));
  TEST_ASSERT_TRUE(update_token_page(device, token, 1794446400ULL,
                                     {false, 1794446400ULL, 0, false}));
  device.page_index = 1;
  const std::string token_layout = device.pages[1].layout_json;
  TEST_ASSERT_TRUE(update_usage_page(device, UsageSnapshot{}, 1794446400ULL,
                                     {true, 0, 1794446460ULL, false}));
  TEST_ASSERT_EQUAL(1, device.page_index);
  TEST_ASSERT_EQUAL_STRING(token_layout.c_str(), device.pages[1].layout_json.c_str());
  TEST_ASSERT_FALSE(update_token_page(device, token, 1794446400ULL,
                                      {false, 1794446400ULL, 0, false}));
  TEST_ASSERT_TRUE(update_token_page(device, token, local_epoch(2026, 11, 13, 0, 1),
                                     {false, 1794446400ULL, 0, false}));
  TEST_ASSERT_EQUAL(1, device.page_index);
}

void test_percent_label_caps_above_999_percent() {
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  for (TokenProvider& provider : snapshot.providers) provider.days.fill(0);
  for (size_t i = 22; i <= 28; i++) snapshot.providers[0].days[i] = 100;
  snapshot.providers[0].days[29] = 999;
  TEST_ASSERT_EQUAL_STRING("today 999 · 999% of a usual day",
      view_of(snapshot, 1794446400ULL).rows[0].today.c_str());
  snapshot.providers[0].days[29] = 1000;
  TEST_ASSERT_EQUAL_STRING("today 1K · 999%+ of a usual day",
      view_of(snapshot, 1794446400ULL).rows[0].today.c_str());
}

void test_never_synced_is_waiting_not_old_data() {
  const TokenView empty = view_of(TokenSnapshot{}, 1794446400ULL);
  TEST_ASSERT_EQUAL_STRING("Waiting for the first numbers.", empty.headline.c_str());
  TEST_ASSERT_EQUAL_STRING("Not synced yet", empty.footer_text.c_str());
  TEST_ASSERT_TRUE(empty.footer_chip.empty());

  TokenSnapshot received;
  TEST_ASSERT_TRUE(decode_token(token_response(), received));
  received.required_source_collected_at = 0;
  const TokenView old = view_of(received, 1794446400ULL);
  TEST_ASSERT_EQUAL_STRING("OLD DATA", old.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("M4 --:--", old.footer_text.c_str());
}

void test_midnight_shift_always_uses_worker_utc8_date() {
  setenv("TZ", "UTC0", 1);
  tzset();
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  // 16:01 UTC is already 00:01 on November 13 in Asia/Shanghai.
  const TokenView shifted = view_of(snapshot, local_epoch(2026, 11, 12, 16, 1));
  setenv("TZ", "SGT-8", 1);
  tzset();
  TEST_ASSERT_EQUAL_UINT64(28000000ULL, shifted.rows[0].shifted_days[28]);
  TEST_ASSERT_EQUAL_UINT64(0ULL, shifted.rows[0].shifted_days[29]);
  TEST_ASSERT_EQUAL_STRING("1.78B", shifted.rows[0].seven_day.c_str());
}

void test_unknown_clock_suppresses_tear_and_never_formats_epoch_retry() {
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  for (TokenProvider& provider : snapshot.providers) provider.days.fill(0);
  for (size_t i = 22; i <= 28; i++) snapshot.providers[0].days[i] = 100;
  snapshot.providers[0].days[29] = 150;
  TEST_ASSERT_EQUAL_STRING("Claude is on a tear today",
      view_of(snapshot, 1794446400ULL).headline.c_str());

  const TokenView unknown = view_of(snapshot, 0, {true, 0, 60, false});
  TEST_ASSERT_EQUAL_STRING("Busiest this week: Claude", unknown.headline.c_str());
  TEST_ASSERT_EQUAL_STRING("NO CLOCK", unknown.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("waiting for NTP", unknown.footer_text.c_str());
  TEST_ASSERT_TRUE(unknown.rows[0].shifted_days == snapshot.providers[0].days);
}

void test_number_format_uses_at_most_three_significant_digits() {
  TEST_ASSERT_EQUAL_STRING("0", format_tokens(0).c_str());
  TEST_ASSERT_EQUAL_STRING("940", format_tokens(940).c_str());
  TEST_ASSERT_EQUAL_STRING("1K", format_tokens(1000).c_str());
  TEST_ASSERT_EQUAL_STRING("10K", format_tokens(9999).c_str());
  TEST_ASSERT_EQUAL_STRING("940K", format_tokens(940000).c_str());
  TEST_ASSERT_EQUAL_STRING("85M", format_tokens(85000000).c_str());
  TEST_ASSERT_EQUAL_STRING("850M", format_tokens(850000000).c_str());
  TEST_ASSERT_EQUAL_STRING("3.9B", format_tokens(3900000000ULL).c_str());
  TEST_ASSERT_EQUAL_STRING("41.2B", format_tokens(41200000000ULL).c_str());
  TEST_ASSERT_EQUAL_STRING("999K", format_tokens(999499).c_str());
  TEST_ASSERT_EQUAL_STRING("1M", format_tokens(999500).c_str());
}

int main() {
  setenv("TZ", "SGT-8", 1);
  tzset();
  UNITY_BEGIN();
  RUN_TEST(test_decoder_accepts_exact_schema_order_and_64_bit_totals);
  RUN_TEST(test_decoder_rejects_malformed_bounded_and_non_integer_responses_without_mutation);
  RUN_TEST(test_derived_values_include_zero_filled_days_and_all_zero_gemini);
  RUN_TEST(test_headline_thresholds_materiality_and_suppression);
  RUN_TEST(test_footer_reports_old_failed_and_syncing_independently);
  RUN_TEST(test_local_midnight_shifts_days_and_makes_new_today_zero);
  RUN_TEST(test_extreme_64_bit_today_share_does_not_overflow_to_negative);
  RUN_TEST(test_two_page_updates_preserve_token_selection_and_dedup_static_frames);
  RUN_TEST(test_percent_label_caps_above_999_percent);
  RUN_TEST(test_never_synced_is_waiting_not_old_data);
  RUN_TEST(test_midnight_shift_always_uses_worker_utc8_date);
  RUN_TEST(test_unknown_clock_suppresses_tear_and_never_formats_epoch_retry);
  RUN_TEST(test_number_format_uses_at_most_three_significant_digits);
  return UNITY_END();
}
