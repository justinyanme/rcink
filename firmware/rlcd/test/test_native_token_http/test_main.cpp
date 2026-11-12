#include <unity.h>

#include "render/token.h"
#include "../fixtures/token_response.h"
#include "../support/config.h"
// Exercise the production HTTP consumers at their HTTPClient/clock boundaries.
#include "../../src/net/usage_client.cpp"
#include "../../src/net/token_client.cpp"

using namespace rcpaper;
namespace rcpaper {
bool wall_clock_is_valid() { return http_fixture::clock_valid; }
}

void reset_token_http() {
  http_fixture::status = 200;
  http_fixture::declared = -1;
  http_fixture::body = token_response();
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

void test_token_url_shares_allowance_origin_and_http_success_uses_strict_decoder() {
  TEST_ASSERT_EQUAL_STRING("https://example.invalid/api/usage-tokens", token_usage_url().c_str());
  reset_token_http();
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(token_fetch(snapshot));
  TEST_ASSERT_EQUAL_UINT64(25900000000ULL, snapshot.providers[0].lifetime);
  TEST_ASSERT_TRUE(http_fixture::ca_set);
  TEST_ASSERT_TRUE(http_fixture::redirects_disabled);
  TEST_ASSERT_EQUAL(1, http_fixture::requests);
}

void test_token_http_failures_preserve_last_complete_snapshot() {
  TokenSnapshot cache;
  TEST_ASSERT_TRUE(decode_token(token_response(), cache));
  const std::string saved = encode_token(cache);
  for (int failure = 0; failure < 6; failure++) {
    reset_token_http();
    switch (failure) {
      case 0: http_fixture::status = 401; break;
      case 1: http_fixture::status = -11; break;
      case 2: http_fixture::body = "{broken"; break;
      case 3: http_fixture::declared = http_fixture::body.size() + 1; break;
      case 4: http_fixture::transfer = "chunked"; break;
      case 5: http_fixture::clock_valid = false; break;
    }
    TEST_ASSERT_FALSE(token_fetch(cache));
    TEST_ASSERT_EQUAL_STRING(saved.c_str(), encode_token(cache).c_str());
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_token_url_shares_allowance_origin_and_http_success_uses_strict_decoder);
  RUN_TEST(test_token_http_failures_preserve_last_complete_snapshot);
  return UNITY_END();
}
