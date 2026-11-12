#include <unity.h>

#include "render/token.h"
#include "render/token_persistence.h"
#include "../fixtures/token_response.h"
#include "../support/Arduino.h"
// Keep production NVS serialization and keys; replace only ESP Preferences IO.
#define ARDUINO 1
#include "../../../shared/rcink-core/src/store/nvs.cpp"

using namespace rcpaper;

void test_token_blob_round_trips_separately_from_allowance_and_axiom() {
  nvs_fixture::values.clear();
  nvs_fixture::fail_write = false;
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  const std::string saved = encode_token(snapshot);
  TEST_ASSERT_TRUE(nvs_save_token(saved));
  TEST_ASSERT_TRUE(nvs_save_usage("allowance-untouched"));
  TEST_ASSERT_EQUAL_STRING("allowance-untouched", nvs_load_usage().c_str());

  snapshot = TokenSnapshot{};
  TEST_ASSERT_TRUE(decode_token(nvs_load_token(), snapshot));
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), encode_token(snapshot).c_str());
  TEST_ASSERT_EQUAL_UINT64(43400000000ULL, snapshot.providers[1].lifetime);
  TEST_ASSERT_EQUAL_UINT64(0ULL, snapshot.providers[2].lifetime);
}

void test_token_blob_rejects_failed_oversized_and_corrupt_writes_or_restores() {
  nvs_fixture::values.clear();
  nvs_fixture::fail_write = false;
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  const std::string saved = encode_token(snapshot);
  TEST_ASSERT_TRUE(nvs_save_token(saved));
  nvs_fixture::fail_write = true;
  TEST_ASSERT_FALSE(nvs_save_token(token_response()));
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), nvs_load_token().c_str());
  nvs_fixture::fail_write = false;
  TEST_ASSERT_FALSE(nvs_save_token(std::string(kTokenMaximumBytes + 1, ' ')));
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), nvs_load_token().c_str());
  nvs_fixture::values["token"] = "{broken";
  TEST_ASSERT_FALSE(decode_token(nvs_load_token(), snapshot));
  TEST_ASSERT_EQUAL_STRING(saved.c_str(), encode_token(snapshot).c_str());
  nvs_fixture::values["token"] = std::string(kTokenMaximumBytes + 1, ' ');
  TEST_ASSERT_TRUE(nvs_load_token().empty());
}

void test_token_persistence_skips_served_at_only_changes() {
  nvs_fixture::values.clear();
  nvs_fixture::fail_write = false;
  TokenSnapshot snapshot;
  TEST_ASSERT_TRUE(decode_token(token_response(), snapshot));
  TEST_ASSERT_TRUE(persist_token_snapshot(snapshot));

  TokenSnapshot newer = snapshot;
  newer.served_at += 60;
  TEST_ASSERT_TRUE(token_content_equal(snapshot, newer));
  TEST_ASSERT_TRUE(persist_token_snapshot(newer));
  TokenSnapshot stored;
  TEST_ASSERT_TRUE(decode_token(nvs_load_token(), stored));
  TEST_ASSERT_EQUAL_UINT64(snapshot.served_at, stored.served_at);  // no rewrite

  newer.providers[0].days[0]++;
  TEST_ASSERT_TRUE(persist_token_snapshot(newer));
  TEST_ASSERT_TRUE(decode_token(nvs_load_token(), stored));
  TEST_ASSERT_EQUAL_UINT64(newer.served_at, stored.served_at);
  TEST_ASSERT_EQUAL_UINT64(newer.providers[0].days[0], stored.providers[0].days[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_token_blob_round_trips_separately_from_allowance_and_axiom);
  RUN_TEST(test_token_blob_rejects_failed_oversized_and_corrupt_writes_or_restores);
  RUN_TEST(test_token_persistence_skips_served_at_only_changes);
  return UNITY_END();
}
