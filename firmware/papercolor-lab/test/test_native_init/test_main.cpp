// The firmware init table equals the pinned library source, copied here
// verbatim from M5GFX 5268353 Panel_ED2208.hpp getInitCommands() list0 and
// Panel_ED2208.cpp _init_sequence() / _turn_on_display().
#include <unity.h>

#include "epd_init.h"

using namespace rclab;

// Panel_ED2208.hpp list0, minus its 0xFF 0xFF terminator.
static const uint8_t kLibraryList0[] = {
    0xAA, 6, 0x49, 0x55, 0x20, 0x08, 0x09, 0x18,  // CMDH
    0x01, 1, 0x3F,
    0x00, 2, 0x5F, 0x69,
    0x05, 4, 0x40, 0x1F, 0x1F, 0x2C,
    0x08, 4, 0x6F, 0x1F, 0x1F, 0x22,
    0x06, 4, 0x6F, 0x1F, 0x17, 0x17,
    0x03, 4, 0x03, 0x54, 0x00, 0x44,

    0x60, 2, 0x02, 0x00,
    0x30, 1, 0x08,
    0x50, 1, 0x3F,

    0xE3, 1, 0x2F,
    0x84, 1, 0x01,
};

// Panel_ED2208.cpp _init_sequence(): 0x61, width>>8, width, height>>8, height
// with cfg.panel_width = 400, cfg.panel_height = 600 (M5GFX.cpp:1895-1896).
static const uint8_t kLibraryResolution[] = {0x61, 4, 0x01, 0x90, 0x02, 0x58};

void test_init_table_matches_library_source() {
  const size_t list0 = sizeof(kLibraryList0), res = sizeof(kLibraryResolution);
  TEST_ASSERT_EQUAL(list0 + res, sizeof(kNativeInit));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(kLibraryList0, kNativeInit, list0);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(kLibraryResolution, kNativeInit + list0, res);
}

void test_booster_after_power_on_matches_library_source() {
  // _turn_on_display(): _send_command(0x06); data 0x6F 0x1F 0x17 0x27.
  const uint8_t library[] = {0x6F, 0x1F, 0x17, 0x27};
  TEST_ASSERT_EQUAL(sizeof(library), sizeof(kBoosterAfterPowerOn));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(library, kBoosterAfterPowerOn, sizeof(library));
  TEST_ASSERT_EQUAL_HEX8(0x06, kCmdBooster);
}

void test_refresh_opcodes_match_library_source() {
  // _turn_on_display(): 0x04 POWER_ON, 0x12 DISPLAY_REFRESH, 0x02 POWER_OFF;
  // _exec_transfer(): 0x10 data start.
  TEST_ASSERT_EQUAL_HEX8(0x04, kCmdPowerOn);
  TEST_ASSERT_EQUAL_HEX8(0x12, kCmdRefresh);
  TEST_ASSERT_EQUAL_HEX8(0x02, kCmdPowerOff);
  TEST_ASSERT_EQUAL_HEX8(0x10, kCmdFrame);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_init_table_matches_library_source);
  RUN_TEST(test_booster_after_power_on_matches_library_source);
  RUN_TEST(test_refresh_opcodes_match_library_source);
  return UNITY_END();
}
