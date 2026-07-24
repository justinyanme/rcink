#include <unity.h>

#include "display/power_policy.h"

using namespace rcpaper;

void test_cold_start_clears_panel() {
  TEST_ASSERT_TRUE(clear_panel_on_startup(PanelStartup::Cold));
}

void test_resume_start_retains_panel() {
  TEST_ASSERT_FALSE(clear_panel_on_startup(PanelStartup::Resume));
}

void test_development_status_is_visible() {
  TEST_ASSERT_TRUE(
      render_fetch_status_on_panel(FirmwareMode::Development));
}

void test_production_status_is_log_only() {
  TEST_ASSERT_FALSE(
      render_fetch_status_on_panel(FirmwareMode::Production));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cold_start_clears_panel);
  RUN_TEST(test_resume_start_retains_panel);
  RUN_TEST(test_development_status_is_visible);
  RUN_TEST(test_production_status_is_log_only);
  return UNITY_END();
}
