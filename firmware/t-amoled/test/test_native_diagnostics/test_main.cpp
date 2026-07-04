#include <unity.h>
#include "render/diagnostics.h"

using namespace rcpaper;

void test_builds_diagnostics_page_from_snapshot() {
  DiagnosticsSnapshot snap;
  snap.free_heap = 123 * 1024;
  snap.free_psram = 456 * 1024;
  snap.battery_ok = true;
  snap.battery_pct = 87;
  snap.battery_mv = 4123;
  snap.wifi_connected = true;
  snap.rssi_dbm = -61;
  snap.last_fetch_s = 1783234567;
  snap.data_fingerprint = 0x1234abcd;
  snap.unchanged_fetches = 2;
  snap.wake_count = 9;
  snap.last_result = "fetch-ok";

  const LayoutNode root = build_diagnostics_page(snap);

  TEST_ASSERT_EQUAL(NodeType::VStack, root.type);
  TEST_ASSERT_EQUAL(9, root.as_stack.children.size());
  TEST_ASSERT_EQUAL_STRING("DIAGNOSTICS", root.as_stack.children[0].as_text.value.c_str());
  TEST_ASSERT_EQUAL_STRING("HEAP 123 KB  PSRAM 456 KB",
                           root.as_stack.children[2].as_text.value.c_str());
  TEST_ASSERT_EQUAL_STRING("BATT 87%  4123mV", root.as_stack.children[3].as_text.value.c_str());
  TEST_ASSERT_EQUAL_STRING("RSSI -61 dBm", root.as_stack.children[4].as_text.value.c_str());
  TEST_ASSERT_EQUAL_STRING("DATA 0x1234abcd  unchanged 2",
                           root.as_stack.children[6].as_text.value.c_str());
  TEST_ASSERT_EQUAL_STRING("RESULT fetch-ok", root.as_stack.children[8].as_text.value.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_builds_diagnostics_page_from_snapshot);
  return UNITY_END();
}
