#include <ArduinoJson.h>
#include <unity.h>
#include <cstdlib>
#include <ctime>
#include <functional>
#include "render/usage.h"
#include "render/usage_view.h"
#include "../fixtures/usage_response.h"

using namespace rcpaper;

// Decode what the page record carries: the renderer draws exactly this.
UsageView view_of(const UsageSnapshot& snapshot, uint64_t now, UsageSync sync = {}) {
  const PageRecord page = usage_page(snapshot, now, sync);
  TEST_ASSERT_EQUAL_STRING(kUsagePageId, page.id.c_str());
  UsageView view;
  TEST_ASSERT_TRUE(decode_usage_view(page.layout_json, view));
  TEST_ASSERT_TRUE(view == derive_usage_view(snapshot, now, sync));
  return view;
}

UsageProvider live(double used, uint64_t now, uint64_t resets_at, const char* id = "claude") {
  UsageProvider p;
  p.id = id;
  p.status = UsageStatus::Ok;
  p.used_percent = used;
  p.fetched_at = now - 180;
  p.expires_at = now + 420;
  p.resets_at = resets_at;
  return p;
}

void test_validated_snapshot_becomes_a_truthful_road() {
  UsageSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_usage(response(), snapshot));
  TEST_ASSERT_EQUAL_UINT64(1794297600, snapshot.providers[0].fetched_at);
  const auto view = view_of(snapshot, 1794297900);
  const auto& claude = view.rows[0];
  TEST_ASSERT_EQUAL((int)UsageRowState::Live, (int)claude.state);
  TEST_ASSERT_EQUAL(66, claude.left);
  TEST_ASSERT_EQUAL_STRING("3d 20h", claude.countdown.c_str());
  TEST_ASSERT_EQUAL(51, claude.car_px);   // 34 % used: the road ahead is the 66 % left
  TEST_ASSERT_EQUAL(67, claude.pace_px);  // 44.7 % of the week has elapsed
  TEST_ASSERT_FALSE(claude.runs_dry);
  TEST_ASSERT_EQUAL_STRING("~24% to spare", claude.verdict.c_str());
  TEST_ASSERT_EQUAL((int)UsageMood::Comfy, (int)claude.mood);
  TEST_ASSERT_TRUE(claude.chip.empty());
  for (size_t i = 1; i < 4; i++) {
    // Never-observed slots: no number, no car, no guessed allowance.
    TEST_ASSERT_EQUAL((int)UsageRowState::Unavailable, (int)view.rows[i].state);
    TEST_ASSERT_EQUAL(-1, view.rows[i].car_px);
    TEST_ASSERT_EQUAL(-1, view.rows[i].pace_px);
    TEST_ASSERT_EQUAL_STRING("--", view.rows[i].countdown.c_str());
    TEST_ASSERT_EQUAL_STRING("no data yet", view.rows[i].verdict.c_str());
  }
  TEST_ASSERT_EQUAL((int)UsageIcon::Spark, (int)view.headline_icon);
  TEST_ASSERT_EQUAL_STRING("Best bet today:", view.headline.c_str());
  TEST_ASSERT_EQUAL(0, view.pick);
  TEST_ASSERT_TRUE(view.footer_chip.empty());
  TEST_ASSERT_EQUAL_STRING("Not synced yet", view.footer_text.c_str());
  TEST_ASSERT_EQUAL_STRING("Synced 16:05",
      view_of(snapshot, 1794297900, {false, 1794297900, 0}).footer_text.c_str());

  // The .5 boundary catches independently rounded percentages summing to 101.
  JsonDocument half;
  deserializeJson(half, response());
  half["providers"][0]["usedPercent"] = 33.5;
  half["providers"][0]["remainingPercent"] = 66.5;
  std::string half_json;
  serializeJson(half, half_json);
  TEST_ASSERT_TRUE(decode_usage(half_json, snapshot));
  TEST_ASSERT_EQUAL(66, view_of(snapshot, 1794297900).rows[0].left);
}

void test_bad_responses_do_not_replace_last_good_visible_values() {
  UsageSnapshot cache;
  TEST_ASSERT_TRUE(decode_usage(response(), cache));
  const auto original = usage_page(cache, 1794297900, {}).layout_json;
  const std::function<void(JsonDocument&)> mutations[] = {
    [](JsonDocument& d) { d["schema"] = "unsupported"; },
    [](JsonDocument& d) { d["extra"] = true; },
    [](JsonDocument& d) { d["providers"][0]["id"] = ""; },
    [](JsonDocument& d) { d["providers"][0]["id"] = "a-way-too-long-provider-id"; },
    [](JsonDocument& d) { d["providers"][0]["id"] = 42; },
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
    TEST_ASSERT_EQUAL_STRING(original.c_str(), usage_page(cache, 1794297900, {}).layout_json.c_str());
  }
  for (auto body : {std::string("{broken"), response() + "garbage", std::string(4097, ' ')}) {
    TEST_ASSERT_FALSE(decode_usage(body, cache));
  }
  // A failed GET keeps the still-valid sample and says so once, in the footer.
  const auto failed = view_of(cache, 1794297900, {true, 1794297000, 1794298200});
  TEST_ASSERT_EQUAL_STRING("SYNC FAILED", failed.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("retry 16:10", failed.footer_text.c_str());
  TEST_ASSERT_EQUAL(66, failed.rows[0].left);
  TEST_ASSERT_EQUAL((int)UsageRowState::Live, (int)failed.rows[0].state);
  TEST_ASSERT_EQUAL_STRING("retrying", view_of(cache, 1794297900, {true, 0, 0}).footer_text.c_str());
  // A requested sync in flight says so, over both a failure and a missing clock.
  for (uint64_t now : {(uint64_t)1794297900, (uint64_t)0}) {
    const auto syncing = view_of(cache, now, {true, 1794297000, 1794298200, true});
    TEST_ASSERT_EQUAL_STRING("SYNCING", syncing.footer_chip.c_str());
    TEST_ASSERT_EQUAL_STRING("fetching now", syncing.footer_text.c_str());
  }
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
  auto view = view_of(snapshot, 1794297900);
  TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)view.rows[0].state);
  TEST_ASSERT_EQUAL(100, view.rows[0].left);           // a real zero-used sample
  TEST_ASSERT_EQUAL_STRING("--", view.rows[0].countdown.c_str());
  TEST_ASSERT_EQUAL(0, view.rows[0].car_px);            // nothing used: car at the start
  TEST_ASSERT_EQUAL(-1, view.rows[0].pace_px);          // null reset: no pace tick
  TEST_ASSERT_EQUAL((int)UsageMood::None, (int)view.rows[0].mood);
  TEST_ASSERT_TRUE(view.rows[0].verdict.empty());       // no projection from stale data
  TEST_ASSERT_TRUE(view.rows[0].chip.empty());          // every known row is stale: footer/headline speak once
  TEST_ASSERT_EQUAL(-1, view.pick);
  // The GET worked but every number is old: say so instead of "Synced".
  TEST_ASSERT_EQUAL_STRING("No new numbers since 16:00.", view.headline.c_str());
  TEST_ASSERT_EQUAL_STRING("OLD DATA", view.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("not synced yet", view.footer_text.c_str());
  view = view_of(snapshot, 1794297900, {false, 1794297840, 0});
  TEST_ASSERT_EQUAL_STRING("OLD DATA", view.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("checked 16:04", view.footer_text.c_str());
  view = view_of(snapshot, 1794297900, {true, 1794297000, 0});
  TEST_ASSERT_EQUAL_STRING("Can't sync. Numbers from 16:00.", view.headline.c_str());
  TEST_ASSERT_EQUAL_STRING("SYNC FAILED", view.footer_chip.c_str());

  TEST_ASSERT_TRUE(decode_usage(response(), snapshot));
  const auto unknown = view_of(snapshot, 0);
  TEST_ASSERT_EQUAL((int)UsageIcon::Clock, (int)unknown.headline_icon);
  TEST_ASSERT_EQUAL_STRING("What day is it? Clock not set.", unknown.headline.c_str());
  TEST_ASSERT_EQUAL_STRING("NO CLOCK", unknown.footer_chip.c_str());
  TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)unknown.rows[0].state);
  TEST_ASSERT_EQUAL(51, unknown.rows[0].car_px);   // the sample is still what was used
  TEST_ASSERT_EQUAL(-1, unknown.rows[0].pace_px);  // but where the week stands is unknown
  TEST_ASSERT_EQUAL_STRING("--", unknown.rows[0].countdown.c_str());
  TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)view_of(snapshot, 1794298200).rows[0].state);

  const auto unavailable = view_of(UsageSnapshot{}, 1794297900);
  for (const auto& row : unavailable.rows) {
    TEST_ASSERT_EQUAL((int)UsageRowState::Unavailable, (int)row.state);
    TEST_ASSERT_EQUAL(-1, row.car_px);
  }
  TEST_ASSERT_EQUAL_STRING("Waiting for the first numbers.", unavailable.headline.c_str());
}

void test_four_rows_keep_independent_freshness_position_and_projection() {
  UsageSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_usage(four_response(), snapshot));
  const auto view = view_of(snapshot, 1794298000);
  const int left[] = {66, 58, 100, 25};
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL(left[i], view.rows[i].left);
  TEST_ASSERT_EQUAL((int)UsageRowState::Live, (int)view.rows[0].state);
  // Codex's own reset has passed without a new sample: stale, pace at the end.
  TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)view.rows[1].state);
  TEST_ASSERT_EQUAL_STRING("due", view.rows[1].countdown.c_str());
  TEST_ASSERT_EQUAL(kUsageRoadPx, view.rows[1].pace_px);
  TEST_ASSERT_EQUAL(63, view.rows[1].car_px);
  TEST_ASSERT_EQUAL_STRING("5m old", view.rows[1].chip.c_str());
  // Kimi has no reset time: honest numbers, no pace tick or projection.
  TEST_ASSERT_EQUAL((int)UsageRowState::Live, (int)view.rows[2].state);
  TEST_ASSERT_EQUAL(0, view.rows[2].car_px);
  TEST_ASSERT_EQUAL(-1, view.rows[2].pace_px);
  TEST_ASSERT_TRUE(view.rows[2].verdict.empty());
  // Gemini burns 75 % in 28 % of its week: the car is far past the pace tick
  // and it runs dry days before the reset.
  const auto& gemini = view.rows[3];
  TEST_ASSERT_EQUAL_STRING("5d 1h", gemini.countdown.c_str());
  TEST_ASSERT_EQUAL(113, gemini.car_px);
  TEST_ASSERT_EQUAL(42, gemini.pace_px);
  TEST_ASSERT_TRUE(gemini.runs_dry);
  TEST_ASSERT_EQUAL_STRING("dry 4d early", gemini.verdict.c_str());
  TEST_ASSERT_EQUAL((int)UsageMood::Hot, (int)gemini.mood);
  TEST_ASSERT_EQUAL(0, view.pick);

  // Source expiry is per row; a fresh GET (servedAt) does not refresh ages.
  const auto later = view_of(snapshot, 1794298380, {true, 1794298000, 1794298680});
  TEST_ASSERT_EQUAL_STRING("13m old", later.rows[0].chip.c_str());
  TEST_ASSERT_EQUAL_STRING("12m old", later.rows[1].chip.c_str());
  TEST_ASSERT_EQUAL((int)UsageRowState::Live, (int)later.rows[2].state);
  TEST_ASSERT_TRUE(later.rows[2].chip.empty());
  TEST_ASSERT_EQUAL_STRING("10m old", later.rows[3].chip.c_str());
  TEST_ASSERT_EQUAL_STRING("retry 16:18", later.footer_text.c_str());
}

void test_headline_recommends_headroom_and_names_who_ran_out() {
  const uint64_t now = 1794362400;  // 2026-11-11 10:00 SGT
  UsageSnapshot s;
  s.count = 4;
  s.providers[0] = live(65.2, now, now + 75 * 3600, "claude");
  s.providers[1] = live(69.8, now, now + 54 * 3600 + 1380, "codex");
  s.providers[2] = live(6.1, now, now + 49 * 3600 + 1680, "kimi");
  s.providers[3] = live(11.0, now, now + 74 * 3600 + 960, "gemini");
  auto view = view_of(s, now);
  TEST_ASSERT_EQUAL_STRING("Best bet today:", view.headline.c_str());
  TEST_ASSERT_EQUAL(2, view.pick);  // Kimi: furthest above its even-pace line
  TEST_ASSERT_EQUAL((int)UsageMood::Hot, (int)view.rows[0].mood);
  TEST_ASSERT_EQUAL_STRING("dry 25h early", view.rows[0].verdict.c_str());
  TEST_ASSERT_EQUAL_STRING("dry 6h early", view.rows[1].verdict.c_str());
  TEST_ASSERT_EQUAL_STRING("~91% to spare", view.rows[2].verdict.c_str());
  TEST_ASSERT_EQUAL((int)UsageMood::Open, (int)view.rows[2].mood);

  s.providers[0] = live(94.3, now, now + 27 * 3600 + 600, "claude");
  s.providers[1] = live(100, now, now + 5 * 3600 + 1200, "codex");
  view = view_of(s, now);
  TEST_ASSERT_EQUAL((int)UsageMood::Nap, (int)view.headline_face);
  TEST_ASSERT_EQUAL_STRING("Codex is napping. Try", view.headline.c_str());
  TEST_ASSERT_EQUAL(2, view.pick);
  TEST_ASSERT_EQUAL_STRING("out of gas", view.rows[1].verdict.c_str());
  TEST_ASSERT_TRUE(view.rows[1].runs_dry);
  TEST_ASSERT_EQUAL(kUsageRoadPx, view.rows[1].car_px);  // parked at the pump
  TEST_ASSERT_EQUAL_STRING("5h", view.rows[1].countdown.c_str());
  TEST_ASSERT_EQUAL((int)UsageMood::Low, (int)view.rows[0].mood);
  TEST_ASSERT_EQUAL_STRING("dry 18h early", view.rows[0].verdict.c_str());

  s.providers[1] = live(69.8, now, now + 54 * 3600, "codex");
  view = view_of(s, now);
  TEST_ASSERT_EQUAL_STRING("Claude is almost out. Try", view.headline.c_str());
  // A stale napping row is not news; the recommendation stays on fresh data.
  s.providers[0].status = UsageStatus::Stale;
  TEST_ASSERT_EQUAL_STRING("Best bet today:", view_of(s, now).headline.c_str());
}

void test_host_refetches_when_samples_expire_and_chases_a_late_source() {
  const uint64_t t = 1794362400;
  UsageSnapshot s;
  TEST_ASSERT_EQUAL_UINT64(0, usage_refetch_at_s(s, t));  // nothing observed yet
  s.count = 2;
  s.providers[0] = live(40, t, t + 86400);                // expires at t + 420
  s.providers[1] = live(20, t, t + 86400, "codex");
  s.providers[1].expires_at = t + 300;
  TEST_ASSERT_EQUAL_UINT64(0, usage_refetch_at_s(s, 0));  // never fetched: normal path
  // Fresh at the last fetch: look again just after the earliest expiry.
  TEST_ASSERT_EQUAL_UINT64(t + 300 + kUsageExpiryGraceS, usage_refetch_at_s(s, t));
  // That fetch found the same expired sample: the source is late, check back in a minute.
  TEST_ASSERT_EQUAL_UINT64(t + 310 + kUsageChaseRetryS, usage_refetch_at_s(s, t + 310));
  // Half an hour after the sample, a quiet source falls back to the normal interval.
  s.providers[0].expires_at = t;
  const uint64_t quiet = s.providers[1].fetched_at + kUsageChaseWindowS;
  TEST_ASSERT_EQUAL_UINT64(0, usage_refetch_at_s(s, quiet));
  TEST_ASSERT_NOT_EQUAL(0, usage_refetch_at_s(s, quiet - 1));
}

void test_frame_stays_put_while_time_passes_within_the_hour() {
  // Battery-mode redraws must follow meaningful changes, not the minute hand.
  const uint64_t now = 1794362400;
  UsageSnapshot s;
  s.count = 3;
  s.providers[0] = live(40, now, now + 80 * 3600 + 1800);
  s.providers[1] = live(20, now, now + 30 * 3600 + 1800, "codex");
  s.providers[2] = live(5, now, now + 100 * 3600 + 1800, "kimi");
  for (size_t i = 0; i < s.count; i++) s.providers[i].expires_at = now + 7200;
  const UsageSync sync = {false, now - 60, 0};
  const auto first = usage_page(s, now, sync).layout_json;
  for (uint64_t t = now + 60; t <= now + 600; t += 60) {
    TEST_ASSERT_EQUAL_STRING(first.c_str(), usage_page(s, t, sync).layout_json.c_str());
  }
  TEST_ASSERT_NOT_EQUAL(0, first.compare(usage_page(s, now + 3600, sync).layout_json));
}

void test_restart_restores_original_observations_without_redrawing_unchanged_page() {
  UsageSnapshot cache;
  TEST_ASSERT_TRUE(decode_usage(four_response(), cache));
  const std::string saved = encode_usage(cache);
  UsageSnapshot restored;
  TEST_ASSERT_TRUE(decode_usage(saved, restored));
  DeviceState device;
  TEST_ASSERT_TRUE(update_usage_page(device, restored, 1794297999, {}));
  const auto first = device.pages[0];
  TEST_ASSERT_FALSE(update_usage_page(device, restored, 1794297999, {}));
  TEST_ASSERT_EQUAL_UINT64(first.data_ts, device.pages[0].data_ts);
  // Error recovery in the same second must still request a new frame.
  TEST_ASSERT_TRUE(update_usage_page(device, restored, 1794297999, {true, 0, 0}));
  TEST_ASSERT_TRUE(update_usage_page(device, restored, 1794297999, {}));
  // Codex's reset passes at ...000 while offline: that row turns stale.
  TEST_ASSERT_TRUE(update_usage_page(device, restored, 1794298000, {}));
  UsageView view;
  TEST_ASSERT_TRUE(decode_usage_view(device.pages[0].layout_json, view));
  TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)view.rows[1].state);
  TEST_ASSERT_TRUE(update_usage_page(device, restored, 1794298200, {true, 0, 0}));
  TEST_ASSERT_TRUE(decode_usage_view(device.pages[0].layout_json, view));
  TEST_ASSERT_EQUAL_STRING("10m old", view.rows[0].chip.c_str());
  TEST_ASSERT_EQUAL((int)UsageRowState::Live, (int)view.rows[2].state);
  TEST_ASSERT_TRUE(update_usage_page(device, restored, 0, {true, 0, 0}));
  TEST_ASSERT_TRUE(decode_usage_view(device.pages[0].layout_json, view));
  for (const auto& row : view.rows) TEST_ASSERT_NOT_EQUAL((int)UsageRowState::Live, (int)row.state);
  TEST_ASSERT_EQUAL_UINT64(1794297660, restored.providers[1].fetched_at);
  TEST_ASSERT_EQUAL_UINT64(1794298000, restored.providers[1].resets_at);
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), encode_usage(restored).c_str());
  TEST_ASSERT_FALSE(decode_usage(saved.substr(0, saved.size() - 1), restored));
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), encode_usage(restored).c_str());
}

void test_only_newer_provider_samples_replace_cache_including_lower_usage_after_source_reset() {
  UsageSnapshot cache;
  TEST_ASSERT_TRUE(decode_usage(four_response(), cache));
  JsonDocument doc;
  deserializeJson(doc, four_response());
  doc["servedAt"] = 1794297900;
  doc["providers"][0]["usedPercent"] = 90;
  doc["providers"][0]["remainingPercent"] = 10;
  doc["providers"][0]["expiresAt"] = 1794298300;
  doc["providers"][0]["resetsAt"] = 1794632500;
  std::string body;
  serializeJson(doc, body);
  UsageSnapshot incoming;
  TEST_ASSERT_TRUE(decode_usage(body, incoming));
  TEST_ASSERT_FALSE(merge_usage(incoming, cache));
  incoming.providers[0].fetched_at--;
  TEST_ASSERT_FALSE(merge_usage(incoming, cache));
  TEST_ASSERT_EQUAL_FLOAT(33.75, cache.providers[0].used_percent);

  // Codex resets at 08:06:40, independently of any calendar week.
  deserializeJson(doc, four_response());
  doc["servedAt"] = 1794298100;
  doc["providers"][1]["fetchedAt"] = 1794298050;
  doc["providers"][1]["expiresAt"] = 1794298650;
  doc["providers"][1]["resetsAt"] = nullptr;
  doc["providers"][1]["usedPercent"] = 0;
  doc["providers"][1]["remainingPercent"] = 100;
  body.clear(); serializeJson(doc, body);
  TEST_ASSERT_TRUE(decode_usage(body, incoming));
  TEST_ASSERT_TRUE(merge_usage(incoming, cache));
  TEST_ASSERT_EQUAL_FLOAT(0, cache.providers[1].used_percent);
  TEST_ASSERT_EQUAL_UINT64(1794298050, cache.providers[1].fetched_at);
  TEST_ASSERT_EQUAL_UINT64(1794297600, cache.providers[0].fetched_at);
  TEST_ASSERT_EQUAL_UINT64(1794297780, cache.providers[3].fetched_at);
  UsageSnapshot restarted;
  TEST_ASSERT_TRUE(decode_usage(encode_usage(cache), restarted));
  const auto view = view_of(restarted, 1794298200);
  TEST_ASSERT_EQUAL((int)UsageRowState::Live, (int)view.rows[1].state);
  TEST_ASSERT_EQUAL(100, view.rows[1].left);
  TEST_ASSERT_EQUAL_STRING("--", view.rows[1].countdown.c_str());
  TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)view.rows[0].state);
  TEST_ASSERT_EQUAL_STRING("10m old", view.rows[0].chip.c_str());
}

void test_later_stale_or_missing_evidence_retains_values_until_a_new_observation() {
  UsageSnapshot cache, incoming;
  TEST_ASSERT_TRUE(decode_usage(response(), cache));
  JsonDocument doc;
  deserializeJson(doc, response());
  doc["servedAt"] = 1794298200;
  doc["providers"][0]["status"] = "stale";
  doc["providers"][0]["usedPercent"] = 90;
  doc["providers"][0]["remainingPercent"] = 10;
  doc["providers"][0]["expiresAt"] = 1794298300;
  doc["providers"][0]["resetsAt"] = 1794632500;
  std::string body;
  serializeJson(doc, body);
  TEST_ASSERT_TRUE(decode_usage(body, incoming));
  TEST_ASSERT_TRUE(merge_usage(incoming, cache));
  auto view = view_of(cache, 1794298199);
  TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)view.rows[0].state);
  TEST_ASSERT_EQUAL(66, view.rows[0].left);
  TEST_ASSERT_EQUAL_UINT64(1794297600, cache.providers[0].fetched_at);
  TEST_ASSERT_EQUAL_UINT64(1794298200, cache.providers[0].expires_at);
  TEST_ASSERT_EQUAL_UINT64(1794632400, cache.providers[0].resets_at);
  TEST_ASSERT_TRUE(decode_usage(response(), incoming));
  TEST_ASSERT_FALSE(merge_usage(incoming, cache));  // equal-time OK cannot undo evidence
  TEST_ASSERT_TRUE(decode_usage(response(), cache));
  UsageSnapshot missing;
  missing.served_at = 1794298000;
  missing.count = 1;
  missing.providers[0].id = "claude";  // upstream: account exists, no data
  TEST_ASSERT_TRUE(merge_usage(missing, cache));
  UsageSnapshot restored;
  TEST_ASSERT_TRUE(decode_usage(encode_usage(cache), restored));
  TEST_ASSERT_EQUAL(1, restored.count);  // rows the source no longer lists drop out
  view = view_of(restored, 1794297999);
  TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)view.rows[0].state);
  TEST_ASSERT_EQUAL(66, view.rows[0].left);
  TEST_ASSERT_EQUAL((int)UsageStatus::Unavailable, (int)restored.providers[1].status);
  incoming.providers[0].fetched_at = 1794298000;
  TEST_ASSERT_TRUE(merge_usage(incoming, restored));
  TEST_ASSERT_EQUAL((int)UsageRowState::Live, (int)view_of(restored, 1794298000).rows[0].state);
  TEST_ASSERT_TRUE(decode_usage(four_response(), cache));
  TEST_ASSERT_TRUE(decode_usage(response(), incoming));
  TEST_ASSERT_TRUE(merge_usage(incoming, cache));
  view = view_of(cache, 1794297900);
  TEST_ASSERT_EQUAL((int)UsageRowState::Live, (int)view.rows[0].state);
  TEST_ASSERT_EQUAL(66, view.rows[0].left);
  TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)view.rows[1].state);
  TEST_ASSERT_EQUAL(58, view.rows[1].left);
  TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)view.rows[2].state);
  TEST_ASSERT_EQUAL(100, view.rows[2].left);
  TEST_ASSERT_EQUAL_UINT64(1794297660, cache.providers[1].fetched_at);
}

void test_provider_list_mirrors_the_source_adding_dropping_and_naming() {
  // Fewer accounts upstream: three rows in payload order, no fixed slots.
  UsageSnapshot three;
  TEST_ASSERT_TRUE(decode_usage(three_response(), three));
  TEST_ASSERT_EQUAL(3, three.count);
  TEST_ASSERT_EQUAL_STRING("codex", three.providers[0].id.c_str());
  TEST_ASSERT_EQUAL_STRING("kimi", three.providers[1].id.c_str());
  TEST_ASSERT_EQUAL_STRING("gemini", three.providers[2].id.c_str());
  auto view = view_of(three, 1794297900);
  TEST_ASSERT_EQUAL(3, view.count);
  TEST_ASSERT_EQUAL_STRING("codex", view.rows[0].id.c_str());
  TEST_ASSERT_EQUAL(58, view.rows[0].left);

  // Merging over a four-account cache drops the removed account's row and
  // takes the newer per-account sample.
  UsageSnapshot cache;
  TEST_ASSERT_TRUE(decode_usage(four_response(), cache));
  TEST_ASSERT_TRUE(merge_usage(three, cache));
  TEST_ASSERT_EQUAL(3, cache.count);
  TEST_ASSERT_EQUAL_STRING("codex", cache.providers[0].id.c_str());
  TEST_ASSERT_EQUAL_UINT64(1794297800, cache.providers[0].fetched_at);
  TEST_ASSERT_EQUAL_UINT64(1794297780, cache.providers[2].fetched_at);
  TEST_ASSERT_EQUAL(3, view_of(cache, 1794297900).count);

  // No accounts at all is a valid, honest empty screen.
  JsonDocument doc;
  deserializeJson(doc, three_response());
  doc["providers"].as<JsonArray>().clear();
  std::string empty;
  serializeJson(doc, empty);
  UsageSnapshot none;
  TEST_ASSERT_TRUE(decode_usage(empty, none));
  TEST_ASSERT_EQUAL(0, none.count);
  view = view_of(none, 1794297900);
  TEST_ASSERT_EQUAL(0, view.count);
  TEST_ASSERT_EQUAL_STRING("Waiting for the first numbers.", view.headline.c_str());

  // Unknown ids decode and get a capitalized display name; the renderer falls
  // back to a generic initial-in-a-disc logo for them.
  doc.clear();
  deserializeJson(doc, three_response());
  doc["providers"][0]["id"] = "zai";
  std::string renamed;
  serializeJson(doc, renamed);
  UsageSnapshot custom;
  TEST_ASSERT_TRUE(decode_usage(renamed, custom));
  TEST_ASSERT_EQUAL_STRING("Zai", usage_provider_name(custom.providers[0].id).c_str());
  TEST_ASSERT_EQUAL_STRING("Codex", usage_provider_name("codex").c_str());
}

void test_persistence_does_not_change_display_rounding_near_a_half_percent() {
  UsageSnapshot cache;
  TEST_ASSERT_TRUE(decode_usage(response(), cache));
  cache.providers[0].used_percent = 49.4999999996;
  UsageSnapshot restored;
  TEST_ASSERT_TRUE(decode_usage(encode_usage(cache), restored));
  TEST_ASSERT_EQUAL(51, view_of(restored, 1794297900).rows[0].left);
}

void test_encoded_view_is_bounded_and_rejects_corruption() {
  UsageSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_usage(four_response(), snapshot));
  const auto json = usage_page(snapshot, 1794298000, {true, 1794298000, 1794298300}).layout_json;
  TEST_ASSERT_LESS_THAN(1024, json.size());
  UsageView out, untouched = derive_usage_view(snapshot, 1794298000, {});
  out = untouched;
  const std::function<void(JsonDocument&)> mutations[] = {
    [](JsonDocument& d) { d["schema"] = "rcink.usage-view.v2"; },  // fixed four slots
    [](JsonDocument& d) { d["rows"][0].as<JsonObject>().remove("id"); },
    [](JsonDocument& d) { d["rows"][0]["car"] = kUsageRoadPx + 1; },
    [](JsonDocument& d) { d["rows"][0]["pace"] = -2; },
    [](JsonDocument& d) { d["rows"][0]["dry"] = 1; },
    [](JsonDocument& d) { d["rows"][0]["left"] = 101; },
    [](JsonDocument& d) { d["rows"][0]["state"] = 3; },
    [](JsonDocument& d) { d["pick"] = 4; },
    [](JsonDocument& d) { d["headline"] = std::string(41, 'x'); },
  };
  for (const auto& mutate : mutations) {
    JsonDocument doc;
    deserializeJson(doc, json);
    mutate(doc);
    std::string bad;
    serializeJson(doc, bad);
    TEST_ASSERT_FALSE(decode_usage_view(bad, out));
    TEST_ASSERT_TRUE(out == untouched);
  }
}

int main() {
  setenv("TZ", "SGT-8", 1);  // the device's LOCAL_TZ
  tzset();
  UNITY_BEGIN();
  RUN_TEST(test_validated_snapshot_becomes_a_truthful_road);
  RUN_TEST(test_bad_responses_do_not_replace_last_good_visible_values);
  RUN_TEST(test_stale_unknown_clock_null_reset_and_real_zero_are_honest);
  RUN_TEST(test_four_rows_keep_independent_freshness_position_and_projection);
  RUN_TEST(test_headline_recommends_headroom_and_names_who_ran_out);
  RUN_TEST(test_host_refetches_when_samples_expire_and_chases_a_late_source);
  RUN_TEST(test_frame_stays_put_while_time_passes_within_the_hour);
  RUN_TEST(test_restart_restores_original_observations_without_redrawing_unchanged_page);
  RUN_TEST(test_only_newer_provider_samples_replace_cache_including_lower_usage_after_source_reset);
  RUN_TEST(test_later_stale_or_missing_evidence_retains_values_until_a_new_observation);
  RUN_TEST(test_provider_list_mirrors_the_source_adding_dropping_and_naming);
  RUN_TEST(test_persistence_does_not_change_display_rounding_near_a_half_percent);
  RUN_TEST(test_encoded_view_is_bounded_and_rejects_corruption);
  return UNITY_END();
}
