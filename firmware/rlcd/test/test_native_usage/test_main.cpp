#include <ArduinoJson.h>
#include <unity.h>
#include "render/usage.h"
#include "proto/parser.h"
#include "render/engine.h"
#include "render/fonts.h"
#include <functional>

using namespace rcpaper;

std::string response() {
  return R"({"schema":"rcink.usage-limits.v1","servedAt":1794297900,"providers":[
    {"id":"claude","status":"ok","usedPercent":33.75,"remainingPercent":66.25,"fetchedAt":1794297600,"expiresAt":1794298200,"resetsAt":1794632400},
    {"id":"codex","status":"unavailable","usedPercent":null,"remainingPercent":null,"fetchedAt":null,"expiresAt":null,"resetsAt":null},
    {"id":"kimi","status":"unavailable","usedPercent":null,"remainingPercent":null,"fetchedAt":null,"expiresAt":null,"resetsAt":null},
    {"id":"gemini","status":"unavailable","usedPercent":null,"remainingPercent":null,"fetchedAt":null,"expiresAt":null,"resetsAt":null}]})";
}
void test_validated_snapshot_becomes_truthful_deck_page() {
  UsageSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_usage(response(), snapshot));
  TEST_ASSERT_EQUAL_UINT64(1794297600, snapshot.providers[0].fetched_at);
  const auto page = usage_page(snapshot, 1794297900, false);
  TEST_ASSERT_EQUAL_STRING("usage-limits", page.id.c_str());
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, page.layout_json));
  const auto root = parse_layout(doc.as<JsonVariantConst>());
  TEST_ASSERT_EQUAL((int)NodeType::VStack, (int)root.type);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, page.layout_json.find("66% left"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, page.layout_json.find("34% used"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, page.layout_json.find("Antigravity"));
  unsigned bars = 0;
  for (JsonObjectConst row : doc["children"].as<JsonArrayConst>()) {
    for (JsonObjectConst node : row["children"].as<JsonArrayConst>()) {
      if (node["type"] == "hbar") { bars++; TEST_ASSERT_EQUAL(66, node["pct"].as<int>()); }
    }
  }
  TEST_ASSERT_EQUAL(1, bars);
  // The .5 boundary catches independently rounded percentages summing to 101.
  JsonDocument half;
  deserializeJson(half, response());
  half["providers"][0]["usedPercent"] = 33.5;
  half["providers"][0]["remainingPercent"] = 66.5;
  std::string half_json;
  serializeJson(half, half_json);
  TEST_ASSERT_TRUE(decode_usage(half_json, snapshot));
  const auto rounded = usage_page(snapshot, 1794297900, false).layout_json;
  TEST_ASSERT_NOT_EQUAL(std::string::npos, rounded.find("66% left"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, rounded.find("34% used"));
}
struct UsageMeasurer : Measurer {
  Size text_size(const TextNode& t) const override {
    const int width = t.size == TextSize::H2 ? 16 : (t.size == TextSize::Body ? 12 : 8);
    return {(int16_t)(t.value.size() * width), font_height(t.size, t.weight)};
  }
  Size icon_size(const IconNode&) const override { return {0, 0}; }
  Size image_size(const ImageNode&) const override { return {0, 0}; }
};
void assert_in_content(const LaidOutNode& node) {
  TEST_ASSERT_NOT_EQUAL((int)NodeType::Unknown, (int)node.node->type);
  TEST_ASSERT_GREATER_OR_EQUAL(0, node.rect.x);
  TEST_ASSERT_GREATER_OR_EQUAL(0, node.rect.y);
  TEST_ASSERT_LESS_OR_EQUAL(344, node.rect.x + node.rect.w);
  TEST_ASSERT_LESS_OR_EQUAL(300, node.rect.y + node.rect.h);
  for (const auto& child : node.children) assert_in_content(child);
}
void test_first_view_fits_beside_existing_sidebar() {
  UsageSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_usage(response(), snapshot));
  JsonDocument doc;
  deserializeJson(doc, usage_page(snapshot, 1794297900, false).layout_json);
  const auto root = parse_layout(doc.as<JsonVariantConst>());
  UsageMeasurer measurer;
  const auto laid = run_layout(root, {0, 0, 344, 300}, measurer, 0.55f);
  assert_in_content(laid);
  unsigned bars = 0;
  for (const auto& row : laid.children) {
    for (const auto& node : row.children) {
      if (node.node->type != NodeType::HBar) continue;
      bars++;
      // draw_hbar needs an inset fill area after the renderer's compaction.
      TEST_ASSERT_GREATER_THAN(4, node.rect.w);
      TEST_ASSERT_GREATER_THAN(4, node.rect.h);
      const int fill_width = (node.rect.w - 4) * (int)node.node->as_hbar.pct / 100;
      TEST_ASSERT_GREATER_THAN(0, fill_width);
      TEST_ASSERT_GREATER_THAN(0, node.rect.h - 4);
    }
  }
  TEST_ASSERT_EQUAL(1, bars);  // Unavailable slots stay bar-free.
}
void test_bad_responses_do_not_replace_last_good_visible_values() {
  UsageSnapshot cache;
  TEST_ASSERT_TRUE(decode_usage(response(), cache));
  const auto original = usage_page(cache, 1794297900, false).layout_json;
  const std::function<void(JsonDocument&)> mutations[] = {
    [](JsonDocument& d) { d["schema"] = "unsupported"; },
    [](JsonDocument& d) { d["extra"] = true; },
    [](JsonDocument& d) { d["providers"].as<JsonArray>().remove(3); },
    [](JsonDocument& d) { d["providers"][1]["id"] = "claude"; },
    [](JsonDocument& d) { d["providers"][0]["usedPercent"] = -1; },
    [](JsonDocument& d) { d["providers"][0]["usedPercent"] = "33.5"; },
    [](JsonDocument& d) { d["providers"][0]["remainingPercent"] = 70; },
    [](JsonDocument& d) { d["providers"][0]["fetchedAt"] = 1794298600; d["providers"][0]["expiresAt"] = 1794299200; },
    [](JsonDocument& d) { d["providers"][0]["fetchedAt"] = 1794297600.5; },
    [](JsonDocument& d) { d["providers"][0]["expiresAt"] = 1; },
    [](JsonDocument& d) { d["providers"][0]["resetsAt"] = 0; },
    [](JsonDocument& d) { d["providers"][0]["status"] = "fresh"; },
    [](JsonDocument& d) { d["providers"][1]["remainingPercent"] = 100; },
    [](JsonDocument& d) { d["providers"][1].as<JsonObject>().remove("resetsAt"); },
    [](JsonDocument& d) { d["servedAt"] = 1794298200; },
  };
  for (const auto& mutate : mutations) {
    JsonDocument doc;
    deserializeJson(doc, response());
    mutate(doc);
    std::string malformed;
    serializeJson(doc, malformed);
    TEST_ASSERT_FALSE(decode_usage(malformed, cache));
    TEST_ASSERT_EQUAL_STRING(original.c_str(), usage_page(cache, 1794297900, false).layout_json.c_str());
  }
  for (auto body : {std::string("{broken"), response() + "garbage", std::string(4097, ' ')}) {
    TEST_ASSERT_FALSE(decode_usage(body, cache));
  }
  const auto failed = usage_page(cache, 1794297900, true).layout_json;
  TEST_ASSERT_NOT_EQUAL(std::string::npos, failed.find("fetch failed"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, failed.find("66% left"));
}
void test_stale_unknown_clock_null_reset_and_real_zero_are_honest() {
  JsonDocument doc;
  deserializeJson(doc, response());
  doc["providers"][0]["status"] = "stale";
  doc["providers"][0]["usedPercent"] = 0;
  doc["providers"][0]["remainingPercent"] = 100;
  doc["providers"][0]["resetsAt"] = nullptr;
  std::string body;
  serializeJson(doc, body);
  UsageSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_usage(body, snapshot));
  const auto page = usage_page(snapshot, 1794297900, false).layout_json;
  TEST_ASSERT_NOT_EQUAL(std::string::npos, page.find("100% left"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, page.find("0% used / STALE"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, page.find("R--"));
  TEST_ASSERT_TRUE(decode_usage(response(), snapshot));
  const auto unknown = usage_page(snapshot, 0, false).layout_json;
  TEST_ASSERT_NOT_EQUAL(std::string::npos, unknown.find("Clock unknown"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, unknown.find("STALE"));
  const auto expired = usage_page(snapshot, 1794298200, false).layout_json;
  TEST_ASSERT_NOT_EQUAL(std::string::npos, expired.find("STALE"));
  const auto unavailable = usage_page(UsageSnapshot{}, 1794297900, false).layout_json;
  TEST_ASSERT_EQUAL(std::string::npos, unavailable.find("hbar"));
  TEST_ASSERT_EQUAL(std::string::npos, unavailable.find("% left"));
}
std::string four_response() {
  return R"({"schema":"rcink.usage-limits.v1","servedAt":1794297900,"providers":[
    {"id":"claude","status":"ok","usedPercent":33.75,"remainingPercent":66.25,"fetchedAt":1794297600,"expiresAt":1794298200,"resetsAt":1794632400},
    {"id":"codex","status":"ok","usedPercent":41.5,"remainingPercent":58.5,"fetchedAt":1794297660,"expiresAt":1794298260,"resetsAt":1794298000},
    {"id":"kimi","status":"ok","usedPercent":0,"remainingPercent":100,"fetchedAt":1794297720,"expiresAt":1794298800,"resetsAt":null},
    {"id":"gemini","status":"ok","usedPercent":75,"remainingPercent":25,"fetchedAt":1794297780,"expiresAt":1794298380,"resetsAt":1794735000}]})";
}
void test_four_rows_show_independent_age_status_reset_and_remaining_bars() {
  UsageSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_usage(four_response(), snapshot));
  JsonDocument doc;
  deserializeJson(doc, usage_page(snapshot, 1794298000, false).layout_json);
  const auto rows = doc["children"].as<JsonArrayConst>();
  TEST_ASSERT_EQUAL(5, rows.size());
  const char* names[] = {"Claude", "Codex", "Kimi", "Gemini (Antigravity)"};
  const char* left[] = {"66% left", "58% left", "100% left", "25% left"};
  const char* detail[] = {
    "34% used / OK / 6m / R11/14 05:00Z",
    "42% used / STALE / 5m / R11/10 08:06Z",
    "0% used / OK / 4m / R--",
    "75% used / OK / 3m / R11/15 09:30Z",
  };
  const int bars[] = {66, 58, 100, 25};
  for (unsigned i = 0; i < 4; i++) {
    const auto row = rows[i + 1]["children"];
    TEST_ASSERT_EQUAL_STRING(names[i], row[0]["children"][0]["value"]);
    TEST_ASSERT_EQUAL_STRING(left[i], row[0]["children"][1]["value"]);
    TEST_ASSERT_EQUAL_STRING(detail[i], row[1]["value"]);
    TEST_ASSERT_EQUAL(bars[i], row[2]["pct"].as<int>());
  }
  // Source expiry affects only that row; fresh GET/servedAt does not alter age.
  const auto expired = usage_page(snapshot, 1794298380, true).layout_json;
  TEST_ASSERT_NOT_EQUAL(std::string::npos, expired.find("75% used / STALE / 10m"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, expired.find("0% used / OK / 11m"));
}
void test_all_four_rows_fit_at_nominal_font_sizes_even_with_clock_warning() {
  UsageSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_usage(four_response(), snapshot));
  UsageMeasurer measurer;
  for (const uint64_t now : {0ULL, 1794298000ULL, 4102444799ULL}) {
    for (const bool failed : {false, true}) {
      JsonDocument doc;
      deserializeJson(doc, usage_page(snapshot, now, failed).layout_json);
      const auto root = parse_layout(doc.as<JsonVariantConst>());
      const auto laid = run_layout(root, {0, 0, 344, 300}, measurer, 0.55f);
      assert_in_content(laid);
      unsigned bars = 0;
      for (const auto& row : laid.children) {
        for (const auto& node : row.children) {
          if (node.node->type == NodeType::HBar) {
            bars++;
            TEST_ASSERT_GREATER_THAN(4, node.rect.h);
          }
        }
      }
      TEST_ASSERT_EQUAL(4, bars);
    }
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_validated_snapshot_becomes_truthful_deck_page);
  RUN_TEST(test_first_view_fits_beside_existing_sidebar);
  RUN_TEST(test_bad_responses_do_not_replace_last_good_visible_values);
  RUN_TEST(test_stale_unknown_clock_null_reset_and_real_zero_are_honest);
  RUN_TEST(test_four_rows_show_independent_age_status_reset_and_remaining_bars);
  RUN_TEST(test_all_four_rows_fit_at_nominal_font_sizes_even_with_clock_warning);
  return UNITY_END();
}
