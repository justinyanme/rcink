#include <ArduinoJson.h>
#include <unity.h>

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

void test_empty_matches_is_ok_with_zero_pages() {
  std::vector<PageRecord> out;
  bool ok = parse_str(R"({"matches": []})", out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(0, out.size());
}

void test_nvs_load_helper_skips_invalid_cached_rows() {
  JsonDocument cache_doc;
  deserializeJson(cache_doc, R"([
    {"id":"","order":1,"data_ts":1780000000,"layout_json":"{\"type\":\"vstack\"}"},
    {"id":"ghost","order":2,"data_ts":1780000000,"layout_json":""},
    {"id":"real","order":3,"data_ts":1780000000,"layout_json":"{\"type\":\"text\",\"value\":\"R\"}"}
  ])");
  std::vector<PageRecord> pages;
  for (JsonVariantConst v : cache_doc.as<JsonArrayConst>()) {
    nvs_append_cached_page(v, pages);
  }
  TEST_ASSERT_EQUAL(1, pages.size());
  TEST_ASSERT_EQUAL_STRING("real", pages[0].id.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"text\",\"value\":\"R\"}", pages[0].layout_json.c_str());
}

void test_missing_envelope_fails() {
  std::vector<PageRecord> out;
  bool ok = parse_str(R"({"format": "legacy"})", out);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL(0, out.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_normal_rows);
  RUN_TEST(test_argmax_bucket_groups_parse_and_sort_by_order);
  RUN_TEST(test_empty_matches_is_ok_with_zero_pages);
  RUN_TEST(test_nvs_load_helper_skips_invalid_cached_rows);
  RUN_TEST(test_missing_envelope_fails);
  return UNITY_END();
}
