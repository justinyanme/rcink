#include <unity.h>

#include "power/wake_reason.h"

using rcpaper::WakeReason;
using rcpaper::kButtonAWakeMask;
using rcpaper::kButtonBWakeMask;
using rcpaper::kButtonCWakeMask;
using rcpaper::wake_reason_from_ext1_mask;

void test_simultaneous_a_b_is_diagnostics_chord() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeReason::ButtonAB),
      static_cast<uint8_t>(wake_reason_from_ext1_mask(kButtonAWakeMask | kButtonBWakeMask)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeReason::ButtonAB),
      static_cast<uint8_t>(
          wake_reason_from_ext1_mask(kButtonAWakeMask | kButtonBWakeMask | kButtonCWakeMask)));
}

void test_single_button_masks_keep_existing_reasons() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeReason::ButtonA),
      static_cast<uint8_t>(wake_reason_from_ext1_mask(kButtonAWakeMask)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeReason::ButtonB),
      static_cast<uint8_t>(wake_reason_from_ext1_mask(kButtonBWakeMask)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeReason::ButtonC),
      static_cast<uint8_t>(wake_reason_from_ext1_mask(kButtonCWakeMask)));
}

void test_non_button_masks_are_unknown() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeReason::Unknown),
      static_cast<uint8_t>(wake_reason_from_ext1_mask(0)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WakeReason::Unknown),
      static_cast<uint8_t>(wake_reason_from_ext1_mask(1ULL << 63)));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_simultaneous_a_b_is_diagnostics_chord);
  RUN_TEST(test_single_button_masks_keep_existing_reasons);
  RUN_TEST(test_non_button_masks_are_unknown);
  return UNITY_END();
}
