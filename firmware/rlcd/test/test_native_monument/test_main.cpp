#include <ArduinoJson.h>
#include <unity.h>

#include <cstdlib>
#include <ctime>
#include <string>

#include "render/monument.h"
#include "render/monument_view.h"

using namespace rcpaper;

namespace {

std::string sample_wire_response(uint64_t end_tokens = 1210982952ULL) {
  JsonDocument doc;
  doc["startDate"] = "2025-11-24";
  doc["endDate"] = "2026-11-23";
  JsonArray days = doc["days"].to<JsonArray>();
  std::tm cur = {};
  cur.tm_year = 2025 - 1900;
  cur.tm_mon = 11 - 1;
  cur.tm_mday = 24;
  cur.tm_hour = 12;
  for (size_t i = 0; i < kMonumentDays; i++) {
    std::tm d = cur;
    d.tm_mday += (int)i;
    mktime(&d);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.tm_year + 1900,
                  d.tm_mon + 1, d.tm_mday);
    JsonObject o = days.add<JsonObject>();
    o["date"] = buf;
    uint64_t val = 0;
    if (i == kMonumentDays - 1) val = end_tokens;
    else if (i >= 300) val = 200000000ULL + (i - 300) * 20000000ULL;
    o["totalTokens"] = val;
  }
  std::string json;
  serializeJson(doc, json);
  return json;
}

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

}  // namespace

void test_decode_wire_response_and_nvs_compact_round_trip() {
  MonumentSnapshot wire;
  std::string reason = "prior";
  TEST_ASSERT_TRUE(decode_monument(sample_wire_response(), wire, &reason));
  TEST_ASSERT_TRUE(reason.empty());
  TEST_ASSERT_EQUAL_STRING("2025-11-24", wire.start_date.c_str());
  TEST_ASSERT_EQUAL_STRING("2026-11-23", wire.end_date.c_str());
  TEST_ASSERT_EQUAL_UINT64(1210982952ULL, wire.days[kMonumentDays - 1]);

  const std::string saved = encode_monument(wire);
  TEST_ASSERT_LESS_THAN(8192, saved.size());
  MonumentSnapshot restored;
  TEST_ASSERT_TRUE(decode_monument(saved, restored, &reason));
  TEST_ASSERT_TRUE(monument_content_equal(wire, restored));
}

void test_decode_rejects_malformed_and_trailing_data() {
  MonumentSnapshot snap;
  std::string reason;
  TEST_ASSERT_FALSE(decode_monument("", snap, &reason));
  TEST_ASSERT_EQUAL_STRING("empty", reason.c_str());

  TEST_ASSERT_FALSE(
      decode_monument(sample_wire_response() + "garbage", snap, &reason));
  TEST_ASSERT_EQUAL_STRING("trailing-data", reason.c_str());

  JsonDocument bad;
  TEST_ASSERT_FALSE(deserializeJson(bad, sample_wire_response()));
  bad["endDate"] = "2026-11-24";  // wrong span (366 days)
  std::string bad_json;
  serializeJson(bad, bad_json);
  TEST_ASSERT_FALSE(decode_monument(bad_json, snap, &reason));
  TEST_ASSERT_EQUAL_STRING("span", reason.c_str());
}

void test_derive_monument_view_clock_heatmap_streak_and_eco_honesty() {
  MonumentSnapshot m;
  TEST_ASSERT_TRUE(decode_monument(sample_wire_response(), m));

  TokenSnapshot tok;
  tok.count = 2;
  tok.today = "2026-11-23";
  tok.providers[0] = {"claude", {}, 25000000000ULL};
  tok.providers[1] = {"codex", {}, 44300000000ULL};

  const uint64_t now = local_epoch(2026, 11, 23, 16, 20);
  const TokenSync sync = {false, now - 600, 0, false, LinkFailure::None, 0,
                          nullptr};

  const MonumentView host_view =
      derive_monument_view(m, tok, now, sync, RuntimeMode::Host);
  TEST_ASSERT_EQUAL_STRING("DESK TIME", host_view.clock_caption.c_str());
  TEST_ASSERT_EQUAL_STRING("16:20", host_view.clock_hhmm.c_str());
  TEST_ASSERT_EQUAL_STRING("MON \xC2\xB7 NOV 23 \xC2\xB7 W48",
                           host_view.header_left.c_str());
  TEST_ASSERT_EQUAL_UINT64(69300000000ULL, host_view.total_odo_value);
  TEST_ASSERT_EQUAL_STRING("69.3B", host_view.total_odo.c_str());
  TEST_ASSERT_EQUAL_STRING("1Y", host_view.window_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(53, host_view.cols);
  TEST_ASSERT_EQUAL_STRING("65d", host_view.active_badge.c_str());
  TEST_ASSERT_EQUAL_STRING("today 1.21B \xC2\xB7 65d streak",
                           host_view.summary_left.c_str());
  TEST_ASSERT_EQUAL_STRING("54.3B \xC2\xB7 peak 1.46B",
                           host_view.summary_right.c_str());
  TEST_ASSERT_EQUAL_STRING("NEXT 100B", host_view.milestone_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(69, host_view.milestone_pct);
  TEST_ASSERT_EQUAL(4, host_view.cells[52 * kMonumentRows + 0]);  // 2026-11-23 Mon
  TEST_ASSERT_EQUAL(kMonumentCellOutside, host_view.cells[52 * kMonumentRows + 1]);

  // Round-trip encoded MonumentView matches byte-for-byte semantically.
  MonumentView decoded;
  const std::string encoded = encode_monument_view(host_view);
  TEST_ASSERT_TRUE(encoded.find("\"rcink.monument-view.v2\"") != std::string::npos);
  TEST_ASSERT_TRUE(decode_monument_view(encoded, decoded));
  TEST_ASSERT_TRUE(decoded == host_view);

  // ECO mode stays truthful: displays LAST SYNC instead of frozen clock.
  const MonumentView eco_view =
      derive_monument_view(m, tok, now, sync, RuntimeMode::Eco);
  TEST_ASSERT_EQUAL_STRING("LAST SYNC", eco_view.clock_caption.c_str());
  TEST_ASSERT_EQUAL_STRING("16:10", eco_view.clock_hhmm.c_str());

  // Midnight shift pushes series forward and preserves uninterrupted streak.
  const uint64_t next_day = local_epoch(2026, 11, 24, 0, 5);
  const MonumentView midnight =
      derive_monument_view(m, tok, next_day, sync, RuntimeMode::Host);
  TEST_ASSERT_EQUAL_STRING("today 0 \xC2\xB7 65d streak",
                           midnight.summary_left.c_str());
}

void test_heatmap_windows_preserve_invariants_and_recompute_window_metrics() {
  // Build a 365-day snapshot with a 220-day uninterrupted streak ending today:
  // - Days 0..144: zero (145 days)
  // - Days 145..272: huge historical burst (128 days at 10B each, peak 20B)
  // - Days 273..364 (last 92 days = 3M): modest ramped values 100M..1.01B
  //   with today (day 364) at 400M (merged with live Token today = 900M).
  MonumentSnapshot m;
  m.start_date = "2025-11-24";
  m.end_date = "2026-11-23";
  m.days.assign(kMonumentDays, 0);
  for (size_t i = 145; i < 273; i++) {
    m.days[i] = (i == 200) ? 20000000000ULL : 10000000000ULL;
  }
  for (size_t i = 273; i < 364; i++) {
    // 91 days ramping 100M .. 1000M in 10M steps.
    m.days[i] = 100000000ULL + (i - 273) * 10000000ULL;
  }
  m.days[364] = 400000000ULL;

  TokenSnapshot tok;
  tok.count = 2;
  tok.today = "2026-11-23";
  tok.providers[0] = {"claude", {}, 500000000000ULL};
  tok.providers[0].days[kTokenDays - 1] = 600000000ULL;
  tok.providers[1] = {"codex", {}, 1000000000000ULL};
  tok.providers[1].days[kTokenDays - 1] = 300000000ULL;
  // Live token today sum = 900M > monument today 400M.

  const uint64_t now = local_epoch(2026, 11, 23, 13, 1);
  const TokenSync sync = {false, now - 300, 0, false, LinkFailure::None, 0,
                          nullptr};

  const MonumentView v1y = derive_monument_view(
      m, tok, now, sync, RuntimeMode::Host, HeatmapWindow::Year);
  const MonumentView v6m = derive_monument_view(
      m, tok, now, sync, RuntimeMode::Host, HeatmapWindow::SixMonths);
  const MonumentView v3m = derive_monument_view(
      m, tok, now, sync, RuntimeMode::Host, HeatmapWindow::ThreeMonths);

  // Window metadata & column geometry.
  TEST_ASSERT_EQUAL_STRING("1Y", v1y.window_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(53, v1y.cols);
  TEST_ASSERT_EQUAL_STRING("6M", v6m.window_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(27, v6m.cols);
  TEST_ASSERT_EQUAL_STRING("3M", v3m.window_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(14, v3m.cols);

  // Active days scoped per window (220 in 1Y, 183 in 6M, 92 in 3M).
  TEST_ASSERT_EQUAL_STRING("220d", v1y.active_badge.c_str());
  TEST_ASSERT_EQUAL_STRING("183d", v6m.active_badge.c_str());
  TEST_ASSERT_EQUAL_STRING("92d", v3m.active_badge.c_str());

  // All-time invariants stay identical across 1Y, 6M, and 3M:
  // Total ODO, milestone target/pct, and 220d streak (NOT clipped to 183 or 92).
  TEST_ASSERT_EQUAL_UINT64(1500000000000ULL, v1y.total_odo_value);
  TEST_ASSERT_EQUAL_UINT64(v1y.total_odo_value, v6m.total_odo_value);
  TEST_ASSERT_EQUAL_UINT64(v1y.total_odo_value, v3m.total_odo_value);
  TEST_ASSERT_EQUAL_STRING("1.5T", v1y.total_odo.c_str());
  TEST_ASSERT_EQUAL_STRING(v1y.total_odo.c_str(), v6m.total_odo.c_str());
  TEST_ASSERT_EQUAL_STRING(v1y.total_odo.c_str(), v3m.total_odo.c_str());
  TEST_ASSERT_EQUAL_STRING("NEXT 2T", v1y.milestone_label.c_str());
  TEST_ASSERT_EQUAL_STRING(v1y.milestone_label.c_str(), v6m.milestone_label.c_str());
  TEST_ASSERT_EQUAL_STRING(v1y.milestone_label.c_str(), v3m.milestone_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(75, v1y.milestone_pct);
  TEST_ASSERT_EQUAL_UINT8(v1y.milestone_pct, v6m.milestone_pct);
  TEST_ASSERT_EQUAL_UINT8(v1y.milestone_pct, v3m.milestone_pct);
  TEST_ASSERT_EQUAL_STRING("today 900M \xC2\xB7 220d streak", v1y.summary_left.c_str());
  TEST_ASSERT_EQUAL_STRING(v1y.summary_left.c_str(), v6m.summary_left.c_str());
  TEST_ASSERT_EQUAL_STRING(v1y.summary_left.c_str(), v3m.summary_left.c_str());

  // Right summary reflects window sum and window peak:
  // In 1Y: peak is 20B (from day 200); in 3M: peak is 1B (day 363).
  TEST_ASSERT_TRUE(v1y.summary_right.find("peak 20B") != std::string::npos);
  TEST_ASSERT_TRUE(v6m.summary_right.find("peak 20B") != std::string::npos);
  TEST_ASSERT_TRUE(v3m.summary_right.find("peak 1B") != std::string::npos);
  TEST_ASSERT_TRUE(v1y.summary_right != v6m.summary_right);
  TEST_ASSERT_TRUE(v6m.summary_right != v3m.summary_right);

  // Window-scoped quartiles: in 1Y, 900M today is dwarfed by 128 days of 10B+
  // (so today is level 2 or 1); in 3M, 900M is in the top quartile (level 4)!
  const uint8_t today_1y = v1y.cells[52 * kMonumentRows + 0];
  const uint8_t today_3m = v3m.cells[13 * kMonumentRows + 0];
  TEST_ASSERT_LESS_THAN_UINT8(4, today_1y);
  TEST_ASSERT_EQUAL_UINT8(4, today_3m);

  // Encode/decode round-trip and cells length == cols * 7 for every preset.
  for (const MonumentView* v : {&v1y, &v6m, &v3m}) {
    const std::string json = encode_monument_view(*v);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, json));
    TEST_ASSERT_EQUAL_STRING("rcink.monument-view.v2", doc["schema"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(v->window_label.c_str(), doc["win"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT8(v->cols, doc["cols"].as<uint8_t>());
    TEST_ASSERT_EQUAL_UINT((size_t)v->cols * kMonumentRows,
                           std::string(doc["cells"].as<const char*>()).size());
    MonumentView restored;
    TEST_ASSERT_TRUE(decode_monument_view(json, restored));
    TEST_ASSERT_TRUE(restored == *v);
  }

  // Render deduplication: unchanged returns false; switching window returns true.
  DeviceState device;
  TEST_ASSERT_TRUE(update_monument_page(device, m, tok, now, sync,
                                        RuntimeMode::Host, HeatmapWindow::Year));
  TEST_ASSERT_FALSE(update_monument_page(device, m, tok, now, sync,
                                         RuntimeMode::Host, HeatmapWindow::Year));
  TEST_ASSERT_TRUE(update_monument_page(device, m, tok, now, sync,
                                        RuntimeMode::Host, HeatmapWindow::SixMonths));
  TEST_ASSERT_FALSE(update_monument_page(device, m, tok, now, sync,
                                         RuntimeMode::Host, HeatmapWindow::SixMonths));
  TEST_ASSERT_TRUE(update_monument_page(device, m, tok, now, sync,
                                        RuntimeMode::Host, HeatmapWindow::ThreeMonths));
}

void test_sunday_safe_calendar_math_across_every_weekday_and_preset() {
  // 2026-11-23 is Monday (dow 0) through 2026-11-29 Sunday (dow 6).
  const HeatmapWindow presets[] = {
      HeatmapWindow::Year,
      HeatmapWindow::SixMonths,
      HeatmapWindow::ThreeMonths,
  };
  const TokenSnapshot empty_tok{};
  const TokenSync sync = {false, 0, 0, false, LinkFailure::None, 0, nullptr};

  for (int offset = 0; offset < 7; offset++) {
    const int day = 23 + offset;  // 2026-11-23 .. 2026-11-29
    const uint64_t now = local_epoch(2026, 11, day, 12, 0);
    MonumentSnapshot m;
    char end_buf[16];
    std::snprintf(end_buf, sizeof(end_buf), "2026-11-%02d", day);
    m.start_date = "2025-11-24";
    m.end_date = end_buf;
    m.days.assign(kMonumentDays, 0);
    m.days[kMonumentDays - 1] = 1000000000ULL;

    for (HeatmapWindow w : presets) {
      const MonumentView v = derive_monument_view(
          m, empty_tok, now, sync, RuntimeMode::Host, w);
      const size_t expected_cols = heatmap_window_cols(w);
      const size_t expected_days = heatmap_window_days(w);
      TEST_ASSERT_EQUAL_UINT8(expected_cols, v.cols);

      // Today MUST land in column (cols - 1) at row == offset (Mon=0..Sun=6).
      const uint8_t today_cell =
          v.cells[(expected_cols - 1) * kMonumentRows + (size_t)offset];
      TEST_ASSERT_EQUAL_UINT8(4, today_cell);

      // On Sunday (offset == 6), every row 0..6 of the final column is inside.
      if (offset == 6) {
        for (size_t r = 0; r < kMonumentRows; r++) {
          TEST_ASSERT_NOT_EQUAL_UINT8(
              kMonumentCellOutside,
              v.cells[(expected_cols - 1) * kMonumentRows + r]);
        }
      }

      // Exact number of inside cells equals window_days; tail beyond cols*7 is outside.
      size_t inside = 0;
      for (size_t i = 0; i < expected_cols * kMonumentRows; i++) {
        if (v.cells[i] != kMonumentCellOutside) inside++;
      }
      TEST_ASSERT_EQUAL_UINT(expected_days, inside);
      for (size_t i = expected_cols * kMonumentRows; i < v.cells.size(); i++) {
        TEST_ASSERT_EQUAL_UINT8(kMonumentCellOutside, v.cells[i]);
      }
    }
  }
}

void test_empty_monument_and_truthful_footers_across_windows() {
  const MonumentSnapshot empty_m;
  const TokenSnapshot empty_tok{};
  const uint64_t valid_now = local_epoch(2026, 11, 24, 10, 30);
  const TokenSync ok_sync = {false, valid_now - 120, 0, false,
                             LinkFailure::None, 0, nullptr};

  // Pre-first-sync with valid clock: empty 0-level grid at selected preset geometry.
  const MonumentView empty_3m = derive_monument_view(
      empty_m, empty_tok, valid_now, ok_sync, RuntimeMode::Host,
      HeatmapWindow::ThreeMonths);
  TEST_ASSERT_EQUAL_STRING("3M", empty_3m.window_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(14, empty_3m.cols);
  TEST_ASSERT_EQUAL_STRING("0d", empty_3m.active_badge.c_str());
  TEST_ASSERT_EQUAL_STRING("Waiting for annual history", empty_3m.summary_left.c_str());
  TEST_ASSERT_TRUE(empty_3m.summary_right.empty());
  size_t zeros = 0;
  for (size_t i = 0; i < 14 * kMonumentRows; i++) {
    if (empty_3m.cells[i] == 0) zeros++;
  }
  TEST_ASSERT_EQUAL_UINT(92, zeros);

  // Pre-first-sync with unset clock: fills cols - 1 empty columns at active preset.
  const MonumentView unset_6m = derive_monument_view(
      empty_m, empty_tok, 0, ok_sync, RuntimeMode::Eco,
      HeatmapWindow::SixMonths);
  TEST_ASSERT_EQUAL_STRING("6M", unset_6m.window_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(27, unset_6m.cols);
  TEST_ASSERT_EQUAL_STRING("NO CLOCK", unset_6m.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("LAST SYNC", unset_6m.clock_caption.c_str());
  TEST_ASSERT_EQUAL_UINT8(0, unset_6m.cells[25 * kMonumentRows + 6]);
  TEST_ASSERT_EQUAL_UINT8(kMonumentCellOutside,
                          unset_6m.cells[26 * kMonumentRows + 0]);

  // Link/Sync/Old-data footers remain truthful across non-default windows.
  const TokenSync wifi_err = {true, 0, valid_now + 60, false,
                              LinkFailure::WifiFailed, 60, "desk-wifi"};
  const MonumentView wifi_3m = derive_monument_view(
      empty_m, empty_tok, valid_now, wifi_err, RuntimeMode::Host,
      HeatmapWindow::ThreeMonths);
  TEST_ASSERT_EQUAL_STRING("NO WIFI", wifi_3m.footer_chip.c_str());

  const TokenSync fetch_err = {true, 0, valid_now + 60, false,
                               LinkFailure::None, 60, nullptr};
  const MonumentView fail_6m = derive_monument_view(
      empty_m, empty_tok, valid_now, fetch_err, RuntimeMode::Host,
      HeatmapWindow::SixMonths);
  TEST_ASSERT_EQUAL_STRING("SYNC FAILED", fail_6m.footer_chip.c_str());

  // Decode rejects v1 schema and mismatched win/cols payloads.
  MonumentView rejected;
  TEST_ASSERT_FALSE(decode_monument_view(
      "{\"schema\":\"rcink.monument-view.v1\",\"cols\":53}", rejected));
  std::string mismatched = encode_monument_view(empty_3m);
  const size_t pos = mismatched.find("\"3M\"");
  TEST_ASSERT_TRUE(pos != std::string::npos);
  mismatched.replace(pos, 4, "\"6M\"");
  TEST_ASSERT_FALSE(decode_monument_view(mismatched, rejected));
}

int main() {
  setenv("TZ", "SGT-8", 1);
  tzset();
  UNITY_BEGIN();
  RUN_TEST(test_decode_wire_response_and_nvs_compact_round_trip);
  RUN_TEST(test_decode_rejects_malformed_and_trailing_data);
  RUN_TEST(test_derive_monument_view_clock_heatmap_streak_and_eco_honesty);
  RUN_TEST(test_heatmap_windows_preserve_invariants_and_recompute_window_metrics);
  RUN_TEST(test_sunday_safe_calendar_math_across_every_weekday_and_preset);
  RUN_TEST(test_empty_monument_and_truthful_footers_across_windows);
  return UNITY_END();
}
