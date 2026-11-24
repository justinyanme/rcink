#include <unity.h>

#include "render/monument.h"
#include "render/monument_persistence.h"
#include "../support/Arduino.h"
// Keep production NVS serialization and keys; replace only ESP Preferences IO.
#define ARDUINO 1
#include "../../../shared/rcink-core/src/store/nvs.cpp"

using namespace rcpaper;

void test_heatmap_window_defaults_to_1y_and_round_trips_presets() {
  nvs_fixture::values.clear();
  nvs_fixture::fail_write = false;

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HeatmapWindow::Year),
                          static_cast<uint8_t>(load_heatmap_window()));

  save_heatmap_window(HeatmapWindow::SixMonths);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HeatmapWindow::SixMonths),
                          static_cast<uint8_t>(load_heatmap_window()));

  save_heatmap_window(HeatmapWindow::ThreeMonths);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HeatmapWindow::ThreeMonths),
                          static_cast<uint8_t>(load_heatmap_window()));

  save_heatmap_window(HeatmapWindow::Year);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HeatmapWindow::Year),
                          static_cast<uint8_t>(load_heatmap_window()));
}

void test_heatmap_window_falls_back_to_1y_on_corrupt_or_out_of_range_bytes() {
  nvs_fixture::values.clear();
  nvs_fixture::fail_write = false;

  nvs_fixture::values["hwin"] = "3";
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HeatmapWindow::Year),
                          static_cast<uint8_t>(load_heatmap_window()));

  nvs_fixture::values["hwin"] = "99";
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HeatmapWindow::Year),
                          static_cast<uint8_t>(load_heatmap_window()));

  nvs_fixture::values["hwin"] = "255";
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HeatmapWindow::Year),
                          static_cast<uint8_t>(load_heatmap_window()));

  // Saving Year heals an out-of-range stored byte back to 0.
  save_heatmap_window(HeatmapWindow::Year);
  TEST_ASSERT_EQUAL_STRING("0", nvs_fixture::values.at("hwin").c_str());
}

void test_heatmap_window_key_is_isolated_from_other_nvs_preferences_and_blobs() {
  nvs_fixture::values.clear();
  nvs_fixture::fail_write = false;

  TEST_ASSERT_TRUE(nvs_save_usage("usage-sentinel"));
  TEST_ASSERT_TRUE(nvs_save_token("token-sentinel"));
  TEST_ASSERT_TRUE(nvs_save_monument("monument-sentinel"));
  nvs_save_dark_mode(true);
  nvs_save_carousel_pinned(true);

  save_heatmap_window(HeatmapWindow::ThreeMonths);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HeatmapWindow::ThreeMonths),
                          static_cast<uint8_t>(load_heatmap_window()));
  TEST_ASSERT_EQUAL_STRING("usage-sentinel", nvs_load_usage().c_str());
  TEST_ASSERT_EQUAL_STRING("token-sentinel", nvs_load_token().c_str());
  TEST_ASSERT_EQUAL_STRING("monument-sentinel", nvs_load_monument().c_str());
  TEST_ASSERT_TRUE(nvs_load_dark_mode(false));
  TEST_ASSERT_TRUE(nvs_load_carousel_pinned(false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_heatmap_window_defaults_to_1y_and_round_trips_presets);
  RUN_TEST(test_heatmap_window_falls_back_to_1y_on_corrupt_or_out_of_range_bytes);
  RUN_TEST(test_heatmap_window_key_is_isolated_from_other_nvs_preferences_and_blobs);
  return UNITY_END();
}
