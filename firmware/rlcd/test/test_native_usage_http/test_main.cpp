#include <cstring>
#include <functional>
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
  http_fixture::status_after_first = 0;
  http_fixture::use_second_status = false;
  http_fixture::declared = -1;
  http_fixture::declared_after_first = -1;
  http_fixture::use_second_declared = false;
  http_fixture::body = response();
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
      case 7: http_fixture::body.replace(http_fixture::body.find("rcink.usage-limits.v2"), 20, "unsupported"); break;
      case 8: http_fixture::body = "{broken"; break;
      case 9: http_fixture::declared = http_fixture::body.size() + 20; break;
      case 10: http_fixture::transfer = "chunked"; break;
    }
    FetchDiag diag;
    TEST_ASSERT_FALSE(usage_fetch(cache, &diag));
    TEST_ASSERT_TRUE(diag.attempted);
    TEST_ASSERT_FALSE(diag.ok);
    TEST_ASSERT_EQUAL_STRING(saved.c_str(), encode_usage(cache).c_str());
    const auto failed = derive_usage_view(cache, 1794298200, {true, 0, 0});
    TEST_ASSERT_EQUAL_STRING("SYNC FAILED", failed.footer_chip.c_str());
    TEST_ASSERT_EQUAL(66, failed.rows[0].left);
    TEST_ASSERT_EQUAL((int)UsageRowState::Stale, (int)failed.rows[0].state);
    TEST_ASSERT_LESS_OR_EQUAL(41000, test_millis);  // 2 transient attempts bounded
    if (failure == 4) {
      TEST_ASSERT_EQUAL(2, diag.attempts);
      TEST_ASSERT_EQUAL(0, strncmp(diag.error, "body:", 5));
    }
  }
  reset_http();
  FetchDiag ok_diag;
  TEST_ASSERT_TRUE(usage_fetch(cache, &ok_diag));
  TEST_ASSERT_TRUE(ok_diag.ok);
  TEST_ASSERT_EQUAL(1, ok_diag.attempts);
  TEST_ASSERT_TRUE(http_fixture::ca_set);
  TEST_ASSERT_TRUE(http_fixture::redirects_disabled);
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), encode_usage(cache).c_str());
}

void test_unknown_clock_prevents_https_and_cannot_make_retained_data_fresh() {
  reset_http();
  UsageSnapshot cache;
  TEST_ASSERT_TRUE(decode_usage(response(), cache));
  http_fixture::clock_valid = false;
  FetchDiag diag;
  TEST_ASSERT_FALSE(usage_fetch(cache, &diag));
  TEST_ASSERT_TRUE(diag.attempted);
  TEST_ASSERT_FALSE(diag.ok);
  TEST_ASSERT_EQUAL(0, http_fixture::dns_requests);
  TEST_ASSERT_EQUAL(0, http_fixture::requests);
  TEST_ASSERT_EQUAL_STRING("config-or-clock", diag.error);
  const auto view = derive_usage_view(cache, 0, {true, 0, 0});
  TEST_ASSERT_EQUAL_STRING("NO CLOCK", view.footer_chip.c_str());
  for (const auto& row : view.rows) TEST_ASSERT_NOT_EQUAL((int)UsageRowState::Live, (int)row.state);
}

void test_transient_failures_retry_once_and_recover_with_timing_diagnostics() {
  for (int err_status : {-11, -1, 502, 503, 504}) {
    reset_http();
    http_fixture::status = err_status;
    http_fixture::status_after_first = 200;
    http_fixture::use_second_status = true;
    http_fixture::dns_ms = 12;
    http_fixture::get_ms = 340;
    UsageSnapshot out;
    FetchDiag diag;
    TEST_ASSERT_TRUE(usage_fetch(out, &diag));
    TEST_ASSERT_EQUAL(2, http_fixture::requests);
    TEST_ASSERT_EQUAL(2, diag.attempts);
    TEST_ASSERT_TRUE(diag.ok);
    TEST_ASSERT_EQUAL(200, diag.status);
    TEST_ASSERT_EQUAL_UINT32(12, diag.dns_ms);
    TEST_ASSERT_EQUAL_UINT32(340, diag.get_ms);
    TEST_ASSERT_EQUAL_STRING("", diag.error);
  }

  // DNS failure on attempt 1 recovers on attempt 2.
  reset_http();
  http_fixture::dns_ok = false;
  http_fixture::dns_ok_after_first = true;
  http_fixture::use_second_dns_ok = true;
  http_fixture::dns_ms = 25;
  http_fixture::get_ms = 110;
  UsageSnapshot out;
  FetchDiag diag;
  TEST_ASSERT_TRUE(usage_fetch(out, &diag));
  TEST_ASSERT_EQUAL(2, http_fixture::dns_requests);
  TEST_ASSERT_EQUAL(1, http_fixture::requests);
  TEST_ASSERT_EQUAL(2, diag.attempts);
  TEST_ASSERT_TRUE(diag.ok);
  TEST_ASSERT_EQUAL_UINT32(25, diag.dns_ms);
  TEST_ASSERT_EQUAL_UINT32(110, diag.get_ms);

  // Truncated body on attempt 1 recovers on attempt 2.
  reset_http();
  http_fixture::declared = (int)http_fixture::body.size() + 20;
  http_fixture::declared_after_first = -1;
  http_fixture::use_second_declared = true;
  UsageSnapshot body_out;
  FetchDiag body_diag;
  TEST_ASSERT_TRUE(usage_fetch(body_out, &body_diag));
  TEST_ASSERT_EQUAL(2, http_fixture::requests);
  TEST_ASSERT_EQUAL(2, body_diag.attempts);
  TEST_ASSERT_TRUE(body_diag.ok);
}

void test_non_transient_failures_never_retry_and_double_transient_stops_at_two() {
  struct NoRetryCase {
    std::function<void()> setup;
    const char* expected_prefix;
  };
  const NoRetryCase cases[] = {
      {[]() { http_fixture::status = 401; }, "http:401"},
      {[]() { http_fixture::status = 429; }, "http:429"},
      {[]() { http_fixture::media = "text/html"; }, "hdr:ct0,te0"},
      {[]() { http_fixture::transfer = "chunked"; }, "hdr:ct1,te1"},
      {[]() { http_fixture::declared = 4097; }, "oversized:4097"},
      {[]() { http_fixture::body = std::string(4097, ' '); }, "oversized:4097"},
      {[]() { http_fixture::body = "{broken"; }, "decode:json-parse:"},
      {[]() {
        http_fixture::body.replace(
            http_fixture::body.find("rcink.usage-limits.v2"), 21, "rcink.usage-limits.v9");
      }, "decode:schema"},
  };
  for (const auto& tc : cases) {
    reset_http();
    tc.setup();
    UsageSnapshot out;
    FetchDiag diag;
    TEST_ASSERT_FALSE(usage_fetch(out, &diag));
    TEST_ASSERT_EQUAL(1, http_fixture::requests);
    TEST_ASSERT_EQUAL(1, diag.attempts);
    TEST_ASSERT_FALSE(diag.ok);
    TEST_ASSERT_EQUAL(0, strncmp(diag.error, tc.expected_prefix, strlen(tc.expected_prefix)));
  }

  // Double transient failure stops after 2 attempts.
  reset_http();
  http_fixture::status = -11;
  http_fixture::status_after_first = 503;
  http_fixture::use_second_status = true;
  UsageSnapshot out;
  FetchDiag diag;
  TEST_ASSERT_FALSE(usage_fetch(out, &diag));
  TEST_ASSERT_EQUAL(2, http_fixture::requests);
  TEST_ASSERT_EQUAL(2, diag.attempts);
  TEST_ASSERT_FALSE(diag.ok);
  TEST_ASSERT_EQUAL_STRING("http:503", diag.error);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_http_failures_keep_last_good_values_and_recovery_uses_original_source_time);
  RUN_TEST(test_unknown_clock_prevents_https_and_cannot_make_retained_data_fresh);
  RUN_TEST(test_transient_failures_retry_once_and_recover_with_timing_diagnostics);
  RUN_TEST(test_non_transient_failures_never_retry_and_double_transient_stops_at_two);
  return UNITY_END();
}
