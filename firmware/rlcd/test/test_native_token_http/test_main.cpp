#include <cstring>
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
  http_fixture::status_after_first = 0;
  http_fixture::use_second_status = false;
  http_fixture::declared = -1;
  http_fixture::declared_after_first = -1;
  http_fixture::use_second_declared = false;
  http_fixture::body = token_response();
  http_fixture::cursor = 0;
  http_fixture::stalled = false;
  http_fixture::stalled_after_first = false;
  http_fixture::use_second_stalled = false;
  http_fixture::dns_ok = true;
  http_fixture::dns_ok_after_first = true;
  http_fixture::use_second_dns_ok = false;
  http_fixture::dns_ms = 0;
  http_fixture::get_ms = 0;
  http_fixture::clock_valid = true;
  http_fixture::ca_set = false;
  http_fixture::requests = 0;
  http_fixture::dns_requests = 0;
  http_fixture::redirects_disabled = false;
  http_fixture::media = "application/json";
  http_fixture::transfer.clear();
  test_millis = 0;
}

void test_token_url_shares_allowance_origin_and_http_success_uses_strict_decoder() {
  TEST_ASSERT_EQUAL_STRING("https://example.invalid/api/usage-tokens?version=3",
                           token_usage_url().c_str());
  reset_token_http();
  http_fixture::body = zai_token_response();
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(token_fetch(snapshot));
  TEST_ASSERT_EQUAL(4, snapshot.count);
  TEST_ASSERT_EQUAL_STRING("zai", snapshot.providers[0].id.c_str());
  TEST_ASSERT_TRUE(snapshot.total_lifetime.has_value());
  TEST_ASSERT_EQUAL_UINT64(7570000000ULL, *snapshot.total_lifetime);
  TEST_ASSERT_TRUE(http_fixture::ca_set);
  TEST_ASSERT_TRUE(http_fixture::redirects_disabled);
  TEST_ASSERT_EQUAL(1, http_fixture::requests);

  reset_token_http();
  http_fixture::body = two_token_response();
  TEST_ASSERT_TRUE(token_fetch(snapshot));
  TEST_ASSERT_EQUAL(2, snapshot.count);
  TEST_ASSERT_FALSE(snapshot.total_lifetime.has_value());
  TEST_ASSERT_EQUAL_STRING("codex", snapshot.providers[0].id.c_str());
  TEST_ASSERT_EQUAL_STRING("gemini", snapshot.providers[1].id.c_str());
  TEST_ASSERT_EQUAL_UINT64(1184080706ULL, snapshot.providers[1].lifetime);
}

void test_token_http_failures_preserve_last_complete_snapshot() {
  TokenSnapshot cache;
  TEST_ASSERT_TRUE(decode_token(zai_token_response(), cache));
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
    FetchDiag diag;
    TEST_ASSERT_FALSE(token_fetch(cache, &diag));
    TEST_ASSERT_TRUE(diag.attempted);
    TEST_ASSERT_FALSE(diag.ok);
    TEST_ASSERT_EQUAL_STRING(saved.c_str(), encode_token(cache).c_str());
    if (failure == 0) {
      TEST_ASSERT_EQUAL(1, diag.attempts);
      TEST_ASSERT_EQUAL_STRING("http:401", diag.error);
    } else if (failure == 2) {
      TEST_ASSERT_EQUAL(1, diag.attempts);
      TEST_ASSERT_EQUAL(0, strncmp(diag.error, "decode:", 7));
    }
  }
}

void test_token_transient_failure_retries_once_and_recovers() {
  reset_token_http();
  http_fixture::status = -11;
  http_fixture::status_after_first = 200;
  http_fixture::use_second_status = true;
  TokenSnapshot snapshot;
  FetchDiag diag;
  TEST_ASSERT_TRUE(token_fetch(snapshot, &diag));
  TEST_ASSERT_EQUAL(2, http_fixture::requests);
  TEST_ASSERT_EQUAL(2, diag.attempts);
  TEST_ASSERT_TRUE(diag.ok);
  TEST_ASSERT_EQUAL(200, diag.status);
  TEST_ASSERT_EQUAL_STRING("", diag.error);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_token_url_shares_allowance_origin_and_http_success_uses_strict_decoder);
  RUN_TEST(test_token_http_failures_preserve_last_complete_snapshot);
  RUN_TEST(test_token_transient_failure_retries_once_and_recovers);
  return UNITY_END();
}
