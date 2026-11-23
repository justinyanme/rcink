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
  TEST_ASSERT_EQUAL_STRING("65 DAYS", host_view.active_badge.c_str());
  TEST_ASSERT_EQUAL_STRING("today 1.21B \xC2\xB7 65d streak",
                           host_view.summary_left.c_str());
  TEST_ASSERT_EQUAL_STRING("peak 1.46B", host_view.summary_right.c_str());
  TEST_ASSERT_EQUAL_STRING("NEXT 100B", host_view.milestone_label.c_str());
  TEST_ASSERT_EQUAL_UINT8(69, host_view.milestone_pct);
  TEST_ASSERT_EQUAL(4, host_view.cells[52 * kMonumentRows + 0]);  // 2026-11-23 Mon
  TEST_ASSERT_EQUAL(kMonumentCellOutside, host_view.cells[52 * kMonumentRows + 1]);

  // Round-trip encoded MonumentView matches byte-for-byte semantically.
  MonumentView decoded;
  TEST_ASSERT_TRUE(decode_monument_view(encode_monument_view(host_view), decoded));
  TEST_ASSERT_TRUE(decoded == host_view);

  // ECO mode stays truthful: displays LAST SYNC instead of frozen clock.
  const MonumentView eco_view =
      derive_monument_view(m, tok, now, sync, RuntimeMode::Eco);
  TEST_ASSERT_EQUAL_STRING("LAST SYNC", eco_view.clock_caption.c_str());
  TEST_ASSERT_EQUAL_STRING("16:10", eco_view.clock_hhmm.c_str());

  // Midnight shift pushes series forward and drops streak if new today is zero.
  const uint64_t next_day = local_epoch(2026, 11, 24, 0, 5);
  const MonumentView midnight =
      derive_monument_view(m, tok, next_day, sync, RuntimeMode::Host);
  TEST_ASSERT_EQUAL_STRING("today 0 \xC2\xB7 65d streak",
                           midnight.summary_left.c_str());
}

int main() {
  setenv("TZ", "SGT-8", 1);
  tzset();
  UNITY_BEGIN();
  RUN_TEST(test_decode_wire_response_and_nvs_compact_round_trip);
  RUN_TEST(test_decode_rejects_malformed_and_trailing_data);
  RUN_TEST(test_derive_monument_view_clock_heatmap_streak_and_eco_honesty);
  return UNITY_END();
}
