#include <unity.h>
#include "render/usage.h"
#include "render/usage_view.h"
#include "../fixtures/usage_response.h"
// Exercise the production HTTP consumer at its HTTPClient/clock boundaries.
#include "../../src/net/usage_client.cpp"

using namespace rcpaper;
namespace rcpaper {
bool wall_clock_is_valid() { return http_fixture::clock_valid; }
}

void reset_http() {
  http_fixture::status = 200;
  http_fixture::declared = -1;
  http_fixture::body = response();
  http_fixture::cursor = 0;
  http_fixture::stalled = false;
  http_fixture::clock_valid = true;
  http_fixture::ca_set = false;
  http_fixture::requests = 0;
  http_fixture::redirects_disabled = false;
  http_fixture::media = "application/json";
  http_fixture::transfer.clear();
  test_millis = 0;
}

void test_http_failures_keep_last_good_values_and_recovery_uses_original_source_time() {
  UsageSnapshot cache;
  TEST_ASSERT_TRUE(decode_usage(response(), cache));
  const auto saved = encode_usage(cache);
  for (int failure = 0; failure < 11; failure++) {
    reset_http();
    switch (failure) {
      case 0: http_fixture::status = -11; break;  // HTTPClient read timeout
      case 1: http_fixture::status = -1; break;   // connection lost
      case 2: http_fixture::status = 401; break;
      case 3: http_fixture::status = 429; break;
      case 4: http_fixture::stalled = true; break;
      case 5: http_fixture::body = std::string(4097, ' '); break;
      case 6: http_fixture::declared = 4097; break;
      case 7: http_fixture::body.replace(http_fixture::body.find("rcink.usage-limits.v1"), 20, "unsupported"); break;
      case 8: http_fixture::body = "{broken"; break;
      case 9: http_fixture::declared = http_fixture::body.size() + 20; break;
      case 10: http_fixture::transfer = "chunked"; break;
    }
    TEST_ASSERT_FALSE(usage_fetch(cache));
    TEST_ASSERT_EQUAL_STRING(saved.c_str(), encode_usage(cache).c_str());
    const auto failed = derive_usage_view(cache, 1794298200, {true, 0, 0});
    TEST_ASSERT_EQUAL_STRING("SYNC FAILED", failed.footer_chip.c_str());
    TEST_ASSERT_EQUAL(66, failed.rows[0].left);
    TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)failed.rows[0].state);
    TEST_ASSERT_LESS_OR_EQUAL(20000, test_millis);  // a stalled body is still bounded
  }
  reset_http();
  TEST_ASSERT_TRUE(usage_fetch(cache));
  TEST_ASSERT_TRUE(http_fixture::ca_set);
  TEST_ASSERT_TRUE(http_fixture::redirects_disabled);
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), encode_usage(cache).c_str());
}

void test_unknown_clock_prevents_https_and_cannot_make_retained_data_fresh() {
  reset_http();
  UsageSnapshot cache;
  TEST_ASSERT_TRUE(decode_usage(response(), cache));
  http_fixture::clock_valid = false;
  TEST_ASSERT_FALSE(usage_fetch(cache));
  TEST_ASSERT_EQUAL(0, http_fixture::requests);
  const auto view = derive_usage_view(cache, 0, {true, 0, 0});
  TEST_ASSERT_EQUAL_STRING("NO CLOCK", view.footer_chip.c_str());
  for (const auto& row : view.rows) TEST_ASSERT_NOT_EQUAL((int)UsageRowState::Live, (int)row.state);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_http_failures_keep_last_good_values_and_recovery_uses_original_source_time);
  RUN_TEST(test_unknown_clock_prevents_https_and_cannot_make_retained_data_fresh);
  return UNITY_END();
}
