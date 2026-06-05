#include <unity.h>
#include <ArduinoJson.h>
#include "proto/envelope.h"

using namespace rcpaper;

JsonDocument doc;

bool parse_str(const char* src, std::vector<PageRecord>& out) {
  doc.clear();
  deserializeJson(doc, src);
  return parse_axiom_envelope(doc.as<JsonVariantConst>(), out);
}

void test_normal_rows() {
  std::vector<PageRecord> out;
  bool ok = parse_str(R"({
    "matches": [
      {"data": {"id": "overview", "order": 5, "data_ts": 1780000000,
                "layout_json": "{\"type\":\"vstack\"}"}},
      {"data": {"id": "claude", "order": 10, "data_ts": 1780000001,
                "layout_json": "{\"type\":\"text\",\"value\":\"X\"}"}}
    ]
  })", out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(2, out.size());
  TEST_ASSERT_EQUAL_STRING("overview", out[0].id.c_str());
  TEST_ASSERT_EQUAL(5, out[0].order);
  TEST_ASSERT_EQUAL_UINT64(1780000000ULL, out[0].data_ts);
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"vstack\"}", out[0].layout_json.c_str());
  TEST_ASSERT_EQUAL_STRING("claude", out[1].id.c_str());
}

// Exact (trimmed) response captured 2026-06-05 for the device's APL when the
// producer had been down >1h: `summarize arg_max(...) by id` over an empty
// window yields one all-null aggregation row, NOT an empty matches[]. This
// must produce zero pages — the caller then keeps its cached pages instead
// of replacing them with an unparseable blank.
void test_empty_window_null_row_yields_no_pages() {
  std::vector<PageRecord> out;
  bool ok = parse_str(R"({
    "format": "legacy",
    "matches": [
      {"_time": "2026-06-04T23:52:58.685023384Z",
       "_sysTime": "1970-01-01T00:00:00Z",
       "_rowId": "0dj0o3z7jwot4-0000000000000000-00000000",
       "data": {"data_ts": null, "id": null, "layout_json": null, "order": null}}
    ]
  })", out);
  TEST_ASSERT_TRUE(ok);  // envelope itself is well-formed...
  TEST_ASSERT_EQUAL(0, out.size());  // ...but the null row is not a page
}

void test_partial_null_row_skipped() {
  // id present but layout_json null (and vice versa) — still unusable.
  std::vector<PageRecord> out;
  bool ok = parse_str(R"({
    "matches": [
      {"data": {"id": "ghost", "order": 1, "data_ts": 1780000000, "layout_json": null}},
      {"data": {"id": null, "order": 2, "data_ts": 1780000000, "layout_json": "{}"}},
      {"data": {"id": "real", "order": 3, "data_ts": 1780000000,
                "layout_json": "{\"type\":\"vstack\"}"}}
    ]
  })", out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(1, out.size());
  TEST_ASSERT_EQUAL_STRING("real", out[0].id.c_str());
}

void test_null_data_row_skipped() {
  std::vector<PageRecord> out;
  bool ok = parse_str(R"({"matches": [{"data": null}, {"_time": "x"}]})", out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(0, out.size());
}

void test_empty_matches_is_ok_with_zero_pages() {
  std::vector<PageRecord> out;
  bool ok = parse_str(R"({"matches": []})", out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(0, out.size());
}

void test_missing_matches_fails() {
  std::vector<PageRecord> out;
  bool ok = parse_str(R"({"buckets": {"series": []}})", out);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL(0, out.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_normal_rows);
  RUN_TEST(test_empty_window_null_row_yields_no_pages);
  RUN_TEST(test_partial_null_row_skipped);
  RUN_TEST(test_null_data_row_skipped);
  RUN_TEST(test_empty_matches_is_ok_with_zero_pages);
  RUN_TEST(test_missing_matches_fails);
  return UNITY_END();
}
