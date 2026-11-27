#include <unity.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "input/interaction_policy.h"
#include "render/neon_scene.h"

using namespace rcpaper;

namespace {

constexpr uint64_t now = 1795779600;  // Fri 2026-11-27 19:40 SGT

struct Desk {
  UsageSnapshot usage{};
  TokenSnapshot token{};
  MonumentSnapshot history{};
  UsageSync u{false, now-60, now+480, false, LinkFailure::None, 60, nullptr};
  TokenSync t{false, now-60, now+900, false, LinkFailure::None, 900, nullptr};
  BatteryReading battery{true, 3820, 73};
  Shtc3Reading env{22.4f, 48, true};
  bool cat = true;
  bool monument_failed = false;
  RuntimeMode mode = RuntimeMode::Host;

  Desk() {
    usage.count = 4;
    usage.providers[0] = {"kimi", UsageStatus::Unavailable};
    usage.providers[1] = {"gemini", UsageStatus::Ok, 12, now-60, now+540, now+86400};
    usage.providers[2] = {"codex", UsageStatus::Ok, 59, now-60, now+540, now+86400};
    usage.providers[3] = {"claude", UsageStatus::Ok, 38, now-60, now+540, now+86400};
    token.today = "2026-11-27";
    token.required_source_collected_at = now-60;
    token.total_lifetime = 123400000;
    history.end_date = token.today;
    history.days.assign(kMonumentDays, 0);
    const int trend[] = {30,55,90,40,70,100,50,35};
    for (size_t i = 0; i < 8; ++i) history.days[kMonumentDays-56 + i*7] = trend[i];
  }

  NeonView view(uint64_t at = now) const {
    return derive_neon_view(usage, u, token, history, t, monument_failed, mode, at,
                            env, battery, cat);
  }
};

void test_rtc_calendar_sensors_allowance_and_eight_week_skyline() {
  Desk d;
  d.usage.providers[3].used_percent = 90;
  d.usage.providers[2].used_percent = 89;
  auto v = d.view();
  TEST_ASSERT_EQUAL_STRING("19:40", v.clock_hhmm.c_str());
  TEST_ASSERT_EQUAL_STRING("FRI", v.weekday.c_str());
  TEST_ASSERT_EQUAL(27, v.day);
  TEST_ASSERT_EQUAL_STRING("22.4", v.env_temp.c_str());
  TEST_ASSERT_EQUAL_STRING("123M", v.total_odo.c_str());
  TEST_ASSERT_EQUAL(4, v.providers.size());
  const std::vector<NeonProvider> expected = {
      {'C', 10, true}, {'X', 11, false}, {'G', 88, false}, {'K', -1, false}};
  TEST_ASSERT_TRUE(v.providers == expected);
  const uint8_t heights[] = {9,17,28,12,22,31,16,11};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(heights, v.skyline_bars.data(), 8);
  d.token.total_lifetime.reset();  // pre-v3 cache, ODO includes all eight weeks
  TEST_ASSERT_EQUAL_STRING("470", d.view().total_odo.c_str());
  d.env.rh = 97;
  TEST_ASSERT_EQUAL_STRING("22.4", d.view().env_temp.c_str());
  d.env.ok = false;
  d.cat = false;
  d.usage.providers[3].status = UsageStatus::Stale;
  v = d.view();
  TEST_ASSERT_FALSE(v.cat_enabled);
  TEST_ASSERT_FALSE(v.providers[0].low_fuel);
  TEST_ASSERT_EQUAL(10, v.providers[0].left);
  TEST_ASSERT_EQUAL_STRING("--", v.env_temp.c_str());

  v = d.view(now + 4*3600 + 20*60);  // RTC crosses midnight between polls
  TEST_ASSERT_EQUAL_STRING("00:00", v.clock_hhmm.c_str());
  TEST_ASSERT_EQUAL_STRING("SAT", v.weekday.c_str());
  TEST_ASSERT_EQUAL(28, v.day);
  v = d.view(0);
  TEST_ASSERT_EQUAL_STRING("--:--", v.clock_hhmm.c_str());
  TEST_ASSERT_TRUE(v.weekday.empty());
  TEST_ASSERT_EQUAL(0, v.day);
}

void status(const NeonView& v, const char* chip, const char* text, bool outage) {
  TEST_ASSERT_EQUAL_STRING(chip, v.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING(text, v.footer_text.c_str());
  TEST_ASSERT_EQUAL(outage, v.outage);
}

void test_footer_precedence_retention_and_outage_independent_of_chip() {
  Desk d;
  d.battery.pct = 7;
  d.u.fetch_failed = d.t.fetch_failed = true;
  d.u.link_failure = LinkFailure::WifiFailed;
  d.t.link_failure = LinkFailure::NtpFailed;
  d.t.syncing = true;
  status(d.view(0), "SYNCING", "fetching now", true);
  d.t.syncing = false;
  status(d.view(0), "NO WIFI", "retry ~1m", true);
  auto retained = d.view();
  status(retained, "NO WIFI", "retry 19:48", true);
  TEST_ASSERT_EQUAL_STRING("19:40", retained.clock_hhmm.c_str());
  d.u.link_failure = LinkFailure::None;
  status(d.view(0), "NO NTP", "retry ~15m", true);
  d.t.link_failure = LinkFailure::None;
  status(d.view(0), "NO CLOCK", "waiting for NTP", false);
  status(d.view(), "SYNC FAILED", "retry 19:48", false);
  TEST_ASSERT_TRUE(d.view().providers == retained.providers);
  TEST_ASSERT_TRUE(d.view().skyline_bars == retained.skyline_bars);
  TEST_ASSERT_EQUAL_STRING(retained.total_odo.c_str(), d.view().total_odo.c_str());
  d.u.fetch_failed = false;
  status(d.view(), "TOKEN FAIL", "retry 19:55", false);
  d.u.fetch_failed = true;
  d.t.fetch_failed = false;
  status(d.view(), "ALLOWANCE", "retry 19:48", false);
  d.u.fetch_failed = false;
  d.token.required_source_collected_at = now-3*3600;
  status(d.view(), "OLD DATA", "M4 16:40", false);
  d.token.required_source_collected_at = 0;
  status(d.view(), "OLD DATA", "M4 --:--", false);
  d.token.required_source_collected_at = now;
  for (int i = 1; i <= 3; ++i) d.usage.providers[i].status = UsageStatus::Stale;
  status(d.view(), "OLD DATA", "checked 19:39", false);
  d.usage.count = 0;
  status(d.view(), "BATTERY 7%", "", false);
  d.battery.pct = 19;
  status(d.view(), "BATTERY 19%", "", false);
  d.battery.pct = 20;
  status(d.view(), "", "", false);
  TEST_ASSERT_FALSE(d.view().battery_low);
  d.battery.ok = false;
  status(d.view(), "BATTERY 20%", "", false);
  TEST_ASSERT_TRUE(d.view().battery_low);
}

void test_codec_and_content_change_time() {
  Desk d;
  auto v = d.view();
  const auto json = encode_neon_view(v);
  for (auto mode : {RuntimeMode::Active, RuntimeMode::Eco}) {
    d.mode = mode;
    TEST_ASSERT_EQUAL_STRING(json.c_str(), encode_neon_view(d.view()).c_str());
  }
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  TEST_ASSERT_EQUAL_STRING("rcink.neon-view.v1", doc["schema"].as<const char*>());
  TEST_ASSERT_TRUE(doc["providers"][3]["left"].isNull());
  NeonView decoded;
  TEST_ASSERT_TRUE(decode_neon_view(json, decoded));
  TEST_ASSERT_TRUE(v == decoded);
  PageRecord page;
  TEST_ASSERT_TRUE(update_neon_page(page, v, now));
  auto hash = usage_page_fingerprint(page);
  d.env.rh = 0;
  TEST_ASSERT_FALSE(update_neon_page(page, d.view(now+59), now+59));
  TEST_ASSERT_EQUAL_UINT64(now, page.data_ts);
  TEST_ASSERT_EQUAL_UINT32(hash, usage_page_fingerprint(page));
  TEST_ASSERT_TRUE(update_neon_page(page, d.view(now+60), now+60));
  TEST_ASSERT_EQUAL_UINT64(now+60, page.data_ts);
  TEST_ASSERT_TRUE(hash != usage_page_fingerprint(page));
  d.cat = false;
  TEST_ASSERT_TRUE(update_neon_page(page, d.view(now+60), now+60));
  auto reject = [&](auto mutate) {
    deserializeJson(doc, json);
    mutate();
    std::string invalid;
    serializeJson(doc, invalid);
    TEST_ASSERT_FALSE(decode_neon_view(invalid, decoded));
    TEST_ASSERT_TRUE(decoded == v);
  };
  reject([&]{ doc["battery_pct"] = 101; });
  reject([&]{ doc["skyline_bars"].remove(0); });
  reject([&]{ doc["providers"][0].remove("left"); });
  reject([&]{ doc["providers"].add<JsonObject>(); });
  reject([&]{ doc["day"] = 32; });
  reject([&]{ doc["clock_hhmm"] = "28:61"; });
  reject([&]{ doc["weekday"] = "BAD"; });
  reject([&]{ doc["providers"][3]["low_fuel"] = true; });
  reject([&]{ doc["outage"] = "true"; });
  reject([&]{ doc["schema"] = "rcink.neon-view.v0"; });
  TEST_ASSERT_FALSE(decode_neon_view(std::string(4096, 'x'), decoded));
  TEST_ASSERT_FALSE(decode_neon_view(json + "{}", decoded));
}

void test_monument_only_failure_follows_token_cadence_and_retains_history() {
  Desk d;
  const auto retained = d.view();
  d.monument_failed = true;
  d.t.retry_at_s = now+300; // next Allowance poll
  status(d.view(), "TOKEN FAIL", "retry 19:54", false);
  d.mode = RuntimeMode::Eco;
  status(d.view(), "TOKEN FAIL", "retry 20:39", false);
  d.u.fetch_failed = true;
  status(d.view(), "SYNC FAILED", "retry 19:48", false);
  TEST_ASSERT_TRUE(d.view().skyline_bars == retained.skyline_bars);
  d.history = {};  // first annual fetch retries on the next shared worker run
  d.u.fetch_failed = false;
  status(d.view(), "TOKEN FAIL", "retry 19:45", false);
  d.monument_failed = false;
  status(d.view(), "", "", false);
}

void test_fifth_page_keys_restore_and_carousel() {
  for (uint8_t i = 0; i < 5; ++i) TEST_ASSERT_EQUAL(i, restored_deck_index(i, 5));
  TEST_ASSERT_EQUAL(0, restored_deck_index(5, 5));
  TEST_ASSERT_EQUAL(0, restored_deck_index(255, 5));
  TEST_ASSERT_EQUAL(4, navigation_after_next_press(View::Deck, 3, 5, false).page_index);
  TEST_ASSERT_EQUAL(0, navigation_after_next_press(View::Deck, 4, 5, false).page_index);
  TEST_ASSERT_EQUAL(4, navigation_after_previous_press(View::Deck, 0, 5, false).page_index);
  TEST_ASSERT_EQUAL(4, navigation_after_sync_press(View::Deck, 4, 5).page_index);
  TEST_ASSERT_TRUE(key_long_action_for_page(View::Deck, kNeonPageId, true) ==
                   KeyLongAction::ToggleCarouselPin);
  TEST_ASSERT_TRUE(host_carousel_due(true, false, false, false, 5, 90000, 0));
  TEST_ASSERT_FALSE(host_carousel_due(true, true, false, false, 5, 90000, 0));
}

bool pixel(const NeonBitmap& bits, int x, int y) {
  return bits[y*50 + x/8] & (0x80 >> (x%8));
}

struct Box {
  int x, y, w, h;
  bool contains(int px, int py) const {
    return px >= x && px < x+w && py >= y && py < y+h;
  }
};

void test_animation_is_bounded_and_static_frame_restores_both_polarities() {
  Desk d;
  d.battery.pct = 7;
  d.usage.providers[3].used_percent = 92;
  const auto v = d.view();
  const Box regions[] = {{0,0,400,112}, {200,224,30,20}, {28,146,10,14},
      {379,211,10,14}, {200,56,5,5}, {200,70,5,5}, {0,268,400,32},
      {33,173,46,8}, {316,166,38,12}};
  NeonBitmap first, animated;
  for (auto pol : {MonoPolarity::Light, MonoPolarity::Dark}) {
    render_neon_scene(v, pol, 0, first);
    bool changed[9]{};
    for (int f : {1,2,4,7,8,35,50,53}) {
      render_neon_scene(v, pol, f, animated);
      for (int y = 0; y < 300; ++y) for (int x = 0; x < 400; ++x) {
        if (pixel(first,x,y) == pixel(animated,x,y)) continue;
        bool allowed = false;
        for (size_t i = 0; i < 9; ++i) {
          // The billboard is opaque to rain; only its two colon cells may move.
          if (i == 0 && Box{112,36,176,72}.contains(x,y)) continue;
          if (regions[i].contains(x,y)) allowed = changed[i] = true;
        }
        TEST_ASSERT_TRUE_MESSAGE(allowed, "animated pixel outside declared regions");
      }
    }
    for (bool moved : changed) TEST_ASSERT_TRUE(moved);
    render_neon_scene(v, pol, 2, animated);
    TEST_ASSERT_TRUE(pixel(animated,33,173));   // bar off at 2 Hz
    TEST_ASSERT_FALSE(pixel(animated,86,173));  // steady !
    TEST_ASSERT_FALSE(pixel(animated,316,166)); // battery on at 1 Hz
    render_neon_scene(v, pol, 4, animated);
    TEST_ASSERT_TRUE(pixel(animated,316,166));
    TEST_ASSERT_TRUE(pixel(animated,200,56));
    render_neon_scene(v, pol, 0, animated);
    TEST_ASSERT_TRUE(first == animated);
  }
}

void test_layout_extremes_and_outage_signs() {
  Desk d;
  auto v = d.view();
  v.weekday = "WED";
  v.day = 31;
  v.env_temp = "-10.5";
  v.total_odo = "999.9B";
  v.footer_chip = "NO CLOCK";
  v.footer_text = "waiting for NTP";
  NeonBitmap lit, dark;
  for (auto pol : {MonoPolarity::Light, MonoPolarity::Dark}) {
    v.outage = false;
    render_neon_scene(v, pol, 0, lit);
    // Outer columns of the widest caption, fourth row, and ODO decimal/suffix.
    TEST_ASSERT_FALSE(pixel(lit,123,86));
    TEST_ASSERT_FALSE(pixel(lit,269,86));
    for (int y = 86; y < 100; ++y) {
      TEST_ASSERT_TRUE(pixel(lit,118,y));  // clear gutters inside billboard border
      TEST_ASSERT_TRUE(pixel(lit,281,y));
    }
    TEST_ASSERT_FALSE(pixel(lit,16,218));
    TEST_ASSERT_FALSE(pixel(lit,79,229));
    TEST_ASSERT_FALSE(pixel(lit,208,250));
    for (int y = 250; y < 264; ++y) TEST_ASSERT_TRUE(pixel(lit,238,y));
    TEST_ASSERT_FALSE(pixel(lit,13,283));
    const int last_detail_x = 8 + (8*12-2) + 10 + 8 + 14*12;
    TEST_ASSERT_EQUAL(pol == MonoPolarity::Light, pixel(lit,last_detail_x,283));
    for (const std::string& chip : {"SYNC FAILED", "BATTERY 100%"}) {
      auto wide = v;
      wide.footer_chip = chip;
      wide.footer_text = chip == "SYNC FAILED" ? "retry 19:48" : "";
      render_neon_scene(wide, pol, 0, dark);
      const int w = 12*chip.size() - 2 + 10;
      TEST_ASSERT_FALSE(pixel(dark,13+(chip.size()-1)*12,283));
      TEST_ASSERT_EQUAL(pol == MonoPolarity::Dark, pixel(dark,8+w,283));
    }
    v.outage = true;
    render_neon_scene(v, pol, 0, dark);
    for (auto sign : {Box{10,140,82,96}, Box{316,166,28,12}, Box{374,166,20,84},
                      Box{318,144,12,14}, Box{258,196,44,16}, Box{100,250,132,14}}) {
      TEST_ASSERT_TRUE(pixel(dark,sign.x,sign.y));
      TEST_ASSERT_FALSE(pixel(dark,sign.x+1,sign.y));
      TEST_ASSERT_TRUE(pixel(dark,sign.x+2,sign.y+2));
    }
    for (auto keep : {Box{116,40,168,64}, Box{340,12,40,40}, Box{264,218,28,48},
                      Box{314,206,30,60}, Box{104,200,126,44}})
      for (int y = keep.y; y < keep.y+keep.h; ++y)
        for (int x = keep.x; x < keep.x+keep.w; ++x)
          TEST_ASSERT_EQUAL(pixel(lit,x,y), pixel(dark,x,y));
    v.outage = false;
    v.cat_enabled = false;
    render_neon_scene(v, pol, 0, dark);
    for (int y = 224; y < 244; ++y)
      for (int x = 200; x < 230; ++x) TEST_ASSERT_FALSE(pixel(dark,x,y));
    v.cat_enabled = true;
  }
}

void test_frame_zero_on_entry_polarity_and_battery_modes() {
  Desk d;
  const auto v = d.view();
  NeonAnimation animation;
  uint32_t ms = UINT32_MAX - 64; // also crosses millis() wrap on the first tick
  NeonBitmap actual, light, dark;
  render_neon_scene(v, MonoPolarity::Light, 0, light);
  render_neon_scene(v, MonoPolarity::Dark, 0, dark);
  TEST_ASSERT_TRUE(light != dark);
  for (auto pol : {MonoPolarity::Light, MonoPolarity::Dark, MonoPolarity::Light}) {
    animation.restart(ms);
    TEST_ASSERT_FALSE(animation.advance(ms+124, RuntimeMode::Host, true));
    render_neon_scene(v, pol, animation.frame, actual);
    const auto& expected = pol == MonoPolarity::Light ? light : dark;
    TEST_ASSERT_TRUE(actual == expected);
    for (int f = 1; f <= 7; ++f) {
      ms += 125;
      TEST_ASSERT_TRUE(animation.advance(ms, RuntimeMode::Host, true));
      TEST_ASSERT_EQUAL(f, animation.frame);
    }
    render_neon_scene(v, pol, animation.frame, actual);
    TEST_ASSERT_TRUE(actual != expected);
    animation.restart(ms);
    for (auto mode : {RuntimeMode::Active, RuntimeMode::Eco, RuntimeMode::Host}) {
      ms += 1000;
      TEST_ASSERT_FALSE(animation.advance(ms, mode, mode != RuntimeMode::Host));
      render_neon_scene(v, pol, animation.frame, actual);
      TEST_ASSERT_TRUE(actual == expected);
    }
  }
}

void test_twelve_native_golden_frames() {
  namespace fs = std::filesystem;
  const auto dir = fs::path(__FILE__).parent_path().parent_path() / "fixtures" / "neon";
  const char* export_dir = std::getenv("RCINK_NEON_EXPORT");
  for (std::string state : {"ok","outage","sync-failure","old-data","low-battery","no-clock"}) {
    Desk d;
    if (state == "outage") d.u.link_failure = LinkFailure::WifiFailed;
    if (state == "sync-failure") d.u.fetch_failed = d.t.fetch_failed = true;
    if (state == "old-data") d.token.required_source_collected_at = now-3*3600;
    if (state == "low-battery") d.battery.pct = 7;
    const auto v = d.view(state == "no-clock" ? 0 : now);
    for (auto pol : {MonoPolarity::Light, MonoPolarity::Dark}) {
      const auto name = state + (pol == MonoPolarity::Light ? "-light.pbm" : "-dark.pbm");
      NeonBitmap scene;
      render_neon_scene(v, pol, 0, scene);
      std::string pbm = "P4\n400 300\n";
      pbm.append(reinterpret_cast<const char*>(scene.data()), scene.size());
      if (export_dir) {
        std::ofstream file(fs::path(export_dir) / name, std::ios::binary);
        TEST_ASSERT_TRUE(file.write(pbm.data(), pbm.size()).good());
      } else {
        std::ifstream file(dir/name, std::ios::binary);
        std::string expected{std::istreambuf_iterator<char>(file), {}};
        TEST_ASSERT_TRUE_MESSAGE(expected == pbm, name.c_str());
      }
    }
  }
}

}  // namespace

int main() {
  setenv("TZ", "SGT-8", 1);
  tzset();
  UNITY_BEGIN();
  RUN_TEST(test_rtc_calendar_sensors_allowance_and_eight_week_skyline);
  RUN_TEST(test_footer_precedence_retention_and_outage_independent_of_chip);
  RUN_TEST(test_codec_and_content_change_time);
  RUN_TEST(test_monument_only_failure_follows_token_cadence_and_retains_history);
  RUN_TEST(test_fifth_page_keys_restore_and_carousel);
  RUN_TEST(test_animation_is_bounded_and_static_frame_restores_both_polarities);
  RUN_TEST(test_layout_extremes_and_outage_signs);
  RUN_TEST(test_frame_zero_on_entry_polarity_and_battery_modes);
  RUN_TEST(test_twelve_native_golden_frames);
  return UNITY_END();
}
