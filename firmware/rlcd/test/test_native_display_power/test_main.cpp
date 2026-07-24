#include <unity.h>

#include "display/pins_config.h"
#include "display/power_policy.h"

using namespace rcpaper;

void assert_gpio_hold(uint8_t pin, uint8_t level) {
  for (const GpioHold& hold : kPreDeepSleepGpioHolds) {
    if (hold.pin == pin) {
      TEST_ASSERT_EQUAL_UINT8(level, hold.level);
      return;
    }
  }
  TEST_FAIL_MESSAGE("required GPIO hold is missing");
}

void test_deep_sleep_keeps_panel_visible_in_low_power_mode() {
  TEST_ASSERT_EQUAL_HEX8(0x39, kPreDeepSleepCommands[0]);
  TEST_ASSERT_EQUAL_HEX8(0x29, kPreDeepSleepCommands[1]);
}

void test_deep_sleep_holds_panel_inputs_at_safe_levels() {
  TEST_ASSERT_EQUAL_UINT(5, kPreDeepSleepGpioHolds.size());
  assert_gpio_hold(ST7305_RST, 1);
  assert_gpio_hold(ST7305_CS, 1);
  assert_gpio_hold(ST7305_SCK, 0);
  assert_gpio_hold(ST7305_MOSI, 0);
  assert_gpio_hold(ST7305_DC, 0);
}

void test_deep_sleep_wake_resumes_without_hardware_reset() {
  const PanelStartup startup = panel_startup_for_deep_sleep_wake(true);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PanelStartup::Resume),
                          static_cast<uint8_t>(startup));
  TEST_ASSERT_FALSE(panel_startup_uses_hardware_reset(startup));
}

void test_deep_sleep_wake_does_not_push_blank_frame() {
  const PanelStartup startup = panel_startup_for_deep_sleep_wake(true);

  TEST_ASSERT_FALSE(panel_startup_pushes_blank_frame(startup));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_deep_sleep_keeps_panel_visible_in_low_power_mode);
  RUN_TEST(test_deep_sleep_holds_panel_inputs_at_safe_levels);
  RUN_TEST(test_deep_sleep_wake_resumes_without_hardware_reset);
  RUN_TEST(test_deep_sleep_wake_does_not_push_blank_frame);
  return UNITY_END();
}
