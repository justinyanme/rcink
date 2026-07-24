#include <unity.h>
#include <ArduinoJson.h>
#include "proto/envelope.h"
#include "store/nvs.h"

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

void test_nvs_load_helper_skips_invalid_cached_rows() {
  JsonDocument doc;
  deserializeJson(doc, R"([
    {"id":"","order":1,"data_ts":1780000000,"layout_json":"{\"type\":\"vstack\"}"},
    {"id":"ghost","order":2,"data_ts":1780000000,"layout_json":""},
    {"id":"real","order":3,"data_ts":1780000000,"layout_json":"{\"type\":\"text\",\"value\":\"R\"}"}
  ])");
  std::vector<PageRecord> pages;
  for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
    nvs_append_cached_page(v, pages);
  }
  TEST_ASSERT_EQUAL(1, pages.size());
  TEST_ASSERT_EQUAL_STRING("real", pages[0].id.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"text\",\"value\":\"R\"}", pages[0].layout_json.c_str());
}

void test_diagnostics_render_replaces_normal_page_marker() {
  DeviceState state;
  state.last_rendered_id = "codex";
  state.last_rendered_data_ts = 1780000000;

  PageRecord codex;
  codex.id = "codex";
  codex.data_ts = 1780000000;
  TEST_ASSERT_TRUE(render_marker_matches(state, codex));

  PageRecord diagnostics;
  diagnostics.id = "diagnostics";
  diagnostics.data_ts = 1780000100;
  update_render_marker(state, diagnostics);

  TEST_ASSERT_TRUE(render_marker_matches(state, diagnostics));
  TEST_ASSERT_FALSE(render_marker_matches(state, codex));
}

void test_empty_matches_is_ok_with_zero_pages() {
  std::vector<PageRecord> out;
  bool ok = parse_str(R"({"matches": []})", out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(0, out.size());
}

void test_argmax_bucket_groups_parse_and_sort_by_order() {
  std::vector<PageRecord> out;
  bool ok = parse_str(R"({
    "matches": [],
    "buckets": {
      "series": [{
        "groups": [
          {"group": {"id": "github"},
           "aggregations": [{"op": "_tmp4", "value": "2026-06-10T04:41:14Z",
             "data": {"order": 50, "data_ts": 1781066474,
                      "layout_json": "{\"type\":\"text\",\"value\":\"G\"}"}}]},
          {"group": {"id": "claude"},
           "aggregations": [{"op": "_tmp4", "value": "2026-06-10T04:41:05Z",
             "data": {"order": 10, "data_ts": 1781066465,
                      "layout_json": "{\"type\":\"text\",\"value\":\"C\"}"}}]}
        ]
      }]
    }
  })", out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(2, out.size());
  TEST_ASSERT_EQUAL_STRING("claude", out[0].id.c_str());
  TEST_ASSERT_EQUAL(10, out[0].order);
  TEST_ASSERT_EQUAL_UINT64(1781066465ULL, out[0].data_ts);
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"text\",\"value\":\"C\"}", out[0].layout_json.c_str());
  TEST_ASSERT_EQUAL_STRING("github", out[1].id.c_str());
}

void test_missing_matches_fails() {
  std::vector<PageRecord> out;
  bool ok = parse_str(R"({"format": "legacy"})", out);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL(0, out.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_normal_rows);
  RUN_TEST(test_empty_window_null_row_yields_no_pages);
  RUN_TEST(test_partial_null_row_skipped);
  RUN_TEST(test_null_data_row_skipped);
  RUN_TEST(test_nvs_load_helper_skips_invalid_cached_rows);
  RUN_TEST(test_diagnostics_render_replaces_normal_page_marker);
  RUN_TEST(test_empty_matches_is_ok_with_zero_pages);
  RUN_TEST(test_argmax_bucket_groups_parse_and_sort_by_order);
  RUN_TEST(test_missing_matches_fails);
  return UNITY_END();
}
