#include <unity.h>
#include "render/usage.h"
#include "render/usage_view.h"
#include "input/navigation.h"
#include "../fixtures/usage_response.h"
#include "../support/Arduino.h"
// Keep production NVS serialization and keys; replace only ESP Preferences IO.
#define ARDUINO 1
#include "../../../shared/rcink-core/src/store/nvs.cpp"

using namespace rcpaper;

void test_nvs_restart_preserves_usage_and_saved_climate_without_replacing_axiom_cache() {
  nvs_fixture::values.clear();
  nvs_fixture::fail_write = false;
  const auto first = nvs_load();
  TEST_ASSERT_FALSE(first.view_saved);
  TEST_ASSERT_EQUAL(1, startup_view_id(first.view_id, true, first.view_saved));
  DeviceState axiom;
  axiom.view_id = 0;  // User chose climate, including an existing pre-usage install.
  axiom.pages = {{"old-axiom", 0, 1794000000, "{\"type\":\"vstack\",\"children\":[]}"}};
  nvs_save(axiom);
  UsageSnapshot cache;
  TEST_ASSERT_TRUE(decode_usage(four_response(), cache));
  const auto saved = encode_usage(cache);
  TEST_ASSERT_TRUE(nvs_save_usage(saved));

  // Discard all RAM state, as on a cold reboot or a timer wake.
  cache = UsageSnapshot{};
  const auto restarted = nvs_load();
  TEST_ASSERT_TRUE(restarted.view_saved);
  TEST_ASSERT_EQUAL(0, startup_view_id(restarted.view_id, true, restarted.view_saved));
  TEST_ASSERT_EQUAL_STRING("old-axiom", restarted.pages[0].id.c_str());
  TEST_ASSERT_TRUE(decode_usage(nvs_load_usage(), cache));
  TEST_ASSERT_EQUAL_UINT64(1794297660, cache.providers[1].fetched_at);
  TEST_ASSERT_EQUAL_UINT64(1794298000, cache.providers[1].resets_at);
  const auto view = derive_usage_view(cache, 1794298000, {});
  TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)view.rows[1].state);
  TEST_ASSERT_EQUAL(58, view.rows[1].left);
  nvs_save_view(1);
  TEST_ASSERT_EQUAL(1, nvs_load().view_id);
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), nvs_load_usage().c_str());
}

void test_failed_or_corrupt_persistence_keeps_last_good_cache_and_bounds_restore() {
  nvs_fixture::values.clear();
  nvs_fixture::fail_write = false;
  UsageSnapshot cache;
  TEST_ASSERT_TRUE(decode_usage(response(), cache));
  const auto saved = encode_usage(cache);
  TEST_ASSERT_TRUE(nvs_save_usage(saved));
  nvs_fixture::fail_write = true;
  TEST_ASSERT_FALSE(nvs_save_usage(four_response()));
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), nvs_load_usage().c_str());
  nvs_fixture::fail_write = false;
  TEST_ASSERT_FALSE(nvs_save_usage(std::string(4097, ' ')));
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), nvs_load_usage().c_str());
  // Simulate damaged/unsupported stored data at the storage boundary.
  nvs_fixture::values["usage"] = "{broken";
  TEST_ASSERT_FALSE(decode_usage(nvs_load_usage(), cache));
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), encode_usage(cache).c_str());
  nvs_fixture::values["usage"] = std::string(4097, ' ');
  TEST_ASSERT_TRUE(nvs_load_usage().empty());
  UsageSnapshot cold;
  TEST_ASSERT_FALSE(decode_usage(nvs_load_usage(), cold));
  for (const auto& row : derive_usage_view(cold, 0, {}).rows) {
    TEST_ASSERT_EQUAL((int)UsageRowState::Unavailable, (int)row.state);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nvs_restart_preserves_usage_and_saved_climate_without_replacing_axiom_cache);
  RUN_TEST(test_failed_or_corrupt_persistence_keeps_last_good_cache_and_bounds_restore);
  return UNITY_END();
}
