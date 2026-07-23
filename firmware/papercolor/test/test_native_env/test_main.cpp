#include <unity.h>
#include "sense/env.h"

using namespace rcpaper;

void test_sht_crc8_known_vector() {
  // SHT4x datasheet appendix: CRC of {0xBE, 0xEF} is 0x92.
  const uint8_t data[] = {0xBE, 0xEF};
  TEST_ASSERT_EQUAL_HEX8(0x92, sht_crc8(data, 2));
}

void test_sht_convert_temperature_bounds() {
  // raw = 0x0000 → -45.0 °C; raw = 0xFFFF → +130.0 °C
  float t, rh;
  sht_convert_raw(0x0000, 0x8000, t, rh);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -45.0f, t);
  sht_convert_raw(0xFFFF, 0x8000, t, rh);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 130.0f, t);
}

void test_sht_convert_humidity_bounds_and_clamp() {
  float t, rh;
  // raw_h = 0x72B0 (≈29360) → ~50%  (derived: (50+6)*65535/125 ≈ 29360)
  sht_convert_raw(0x4000, 0x72B0, t, rh);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, rh);

  // raw_h = 0xFFFF → 100% (datasheet clamp; raw 0xFFFF maps to 100.0 exactly)
  sht_convert_raw(0x4000, 0xFFFF, t, rh);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, rh);

  // raw_h = 0x0000 → 0% (clamp; the linear formula yields a small negative)
  sht_convert_raw(0x4000, 0x0000, t, rh);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, rh);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sht_crc8_known_vector);
  RUN_TEST(test_sht_convert_temperature_bounds);
  RUN_TEST(test_sht_convert_humidity_bounds_and_clamp);
  return UNITY_END();
}
