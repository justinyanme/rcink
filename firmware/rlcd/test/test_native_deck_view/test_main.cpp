#include <unity.h>

#include "render/deck_view.h"

using namespace rcpaper;

static DeckViewKey sample_key() {
  return {"overview", 100, 1, 6, 80, true, false,
          RuntimeMode::Eco, true};
}

void test_identical_deck_view_keys_match() {
  TEST_ASSERT_TRUE(sample_key() == sample_key());
}

void test_every_visible_field_invalidates_the_deck_view_key() {
  const DeckViewKey original = sample_key();

  DeckViewKey changed = original;
  changed.page_id = "weather";
  TEST_ASSERT_TRUE(changed != original);
  changed = original;
  changed.data_ts++;
  TEST_ASSERT_TRUE(changed != original);
  changed = original;
  changed.page_index++;
  TEST_ASSERT_TRUE(changed != original);
  changed = original;
  changed.page_count++;
  TEST_ASSERT_TRUE(changed != original);
  changed = original;
  changed.battery_pct--;
  TEST_ASSERT_TRUE(changed != original);
  changed = original;
  changed.battery_ok = false;
  TEST_ASSERT_TRUE(changed != original);
  changed = original;
  changed.stale = true;
  TEST_ASSERT_TRUE(changed != original);
  changed = original;
  changed.runtime_mode = RuntimeMode::Host;
  TEST_ASSERT_TRUE(changed != original);
  changed = original;
  changed.dark_mode = false;
  TEST_ASSERT_TRUE(changed != original);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_identical_deck_view_keys_match);
  RUN_TEST(test_every_visible_field_invalidates_the_deck_view_key);
  return UNITY_END();
}
