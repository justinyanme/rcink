#include <unity.h>
#include "power/supply.h"

using namespace rcpaper;

void test_datasheet_source_bitmap_and_captured_usb_battery() {
  // Oracle: M5PM1 datasheet v1.9 p12, bits0/1 external, bit2 battery.
  // 0x05 was captured on this PaperColor with USB attached, VIN=4598 mV.
  const struct { uint8_t flags; Supply expected; } cases[] = {
    {0x00, Supply::Unknown}, {0x01, Supply::External},
    {0x02, Supply::External}, {0x03, Supply::External},
    {0x04, Supply::Battery}, {0x05, Supply::External},
    {0x06, Supply::External}, {0x07, Supply::External},
    {0x80, Supply::Unknown}, {0x84, Supply::Battery},
  };
  for (const auto& c : cases) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(c.expected),
                          static_cast<int>(decode_supply(c.flags)));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_datasheet_source_bitmap_and_captured_usb_battery);
  return UNITY_END();
}
