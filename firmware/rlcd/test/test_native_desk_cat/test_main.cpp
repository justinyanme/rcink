#include <unity.h>

#include <cmath>
#include <cstdint>

#include "render/desk_cat.h"

using namespace rcpaper;

void test_desk_cat_stays_within_ledge_and_yarn_bounds_across_long_run() {
  DeskCatState cat = desk_cat_initial(0x12345678u);
  const DeskCatContext active_day = {RuntimeMode::Host, 12 * 60 + 30, true};

  int jumps_started = 0;
  int jumps_completed = 0;
  int yarn_ticks = 0;
  int walk_ticks = 0;
  bool in_jump = false;
  uint8_t jump_origin = 0;
  int jump_steps = 0;

  for (int tick = 0; tick < 800; tick++) {
    const DeskCatMode prev_mode = cat.mode;
    const uint8_t prev_ledge = cat.ledge_idx;
    step_desk_cat(cat, active_day);

    if (!in_jump && cat.mode == DeskCatMode::Jump) {
      in_jump = true;
      jump_origin = prev_ledge;
      jump_steps = 0;
      jumps_started++;
      TEST_ASSERT_NOT_EQUAL(jump_origin, cat.j_target_ledge);
      TEST_ASSERT_LESS_THAN(kDeskCatLedgeCount, cat.j_target_ledge);
    } else if (in_jump) {
      jump_steps++;
      if (cat.mode != DeskCatMode::Jump) {
        in_jump = false;
        jumps_completed++;
        TEST_ASSERT_LESS_OR_EQUAL(kDeskCatJumpDuration, jump_steps);
        TEST_ASSERT_NOT_EQUAL(jump_origin, cat.ledge_idx);
        const DeskCatLedge& landed = kDeskCatLedges[cat.ledge_idx];
        TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)landed.y, cat.y);
        TEST_ASSERT_TRUE(cat.x >= (float)landed.min_x &&
                         cat.x <= (float)landed.max_x);
      }
    }

    if (cat.mode != DeskCatMode::Jump) {
      TEST_ASSERT_LESS_THAN(kDeskCatLedgeCount, cat.ledge_idx);
      const DeskCatLedge& ledge = kDeskCatLedges[cat.ledge_idx];
      TEST_ASSERT_TRUE(cat.x >= (float)ledge.min_x &&
                       cat.x <= (float)ledge.max_x);
      TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)ledge.y, cat.y);
      if (cat.mode == DeskCatMode::PlayYarn && cat.yarn_active) {
        yarn_ticks++;
        TEST_ASSERT_TRUE(cat.yarn_x >= (float)ledge.min_x &&
                         cat.yarn_x <= (float)ledge.max_x);
      }
      if (cat.mode == DeskCatMode::Walk) walk_ticks++;
    } else {
      TEST_ASSERT_FALSE(cat.yarn_active);
      TEST_ASSERT_TRUE(cat.x >= 0.0f && cat.x <= 400.0f);
      TEST_ASSERT_TRUE(cat.y >= 0.0f && cat.y <= 300.0f);
    }
    (void)prev_mode;
  }

  TEST_ASSERT_GREATER_THAN(3, jumps_started);
  TEST_ASSERT_GREATER_OR_EQUAL(jumps_started - 1, jumps_completed);
  TEST_ASSERT_GREATER_THAN(20, yarn_ticks);
  TEST_ASSERT_GREATER_THAN(10, walk_ticks);
}

void test_desk_cat_active_and_eco_never_mutate_state_and_render_static_pose() {
  DeskCatState cat = desk_cat_initial(0xCAFEBABEu);
  cat.mode = DeskCatMode::Walk;
  cat.x = 180.0f;
  cat.y = (float)kDeskCatLedges[1].y;
  cat.timer = 1;

  for (RuntimeMode mode : {RuntimeMode::Active, RuntimeMode::Eco}) {
    const float orig_x = cat.x;
    const float orig_y = cat.y;
    const uint8_t orig_ledge = cat.ledge_idx;
    const DeskCatMode orig_mode = cat.mode;
    const int16_t orig_timer = cat.timer;
    const DeskCatContext ctx = {mode, 14 * 60, true};
    for (int i = 0; i < 40; i++) step_desk_cat(cat, ctx);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, orig_x, cat.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, orig_y, cat.y);
    TEST_ASSERT_EQUAL_UINT8(orig_ledge, cat.ledge_idx);
    TEST_ASSERT_EQUAL(orig_mode, cat.mode);
    TEST_ASSERT_EQUAL_INT16(orig_timer, cat.timer);
  }

  // Static render pose in ACTIVE/ECO is deterministic SIT on a valid ledge.
  const DeskCatState pose = desk_cat_render_pose(cat, false);
  TEST_ASSERT_EQUAL(DeskCatMode::Sit, pose.mode);
  TEST_ASSERT_FALSE(pose.yarn_active);
  const DeskCatLedge& ledge = kDeskCatLedges[pose.ledge_idx];
  TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)ledge.y, pose.y);
  TEST_ASSERT_TRUE(pose.x >= (float)ledge.min_x && pose.x <= (float)ledge.max_x);
}

void test_desk_cat_mood_weights_favor_sleep_at_night_and_play_on_active_days() {
  DeskCatState night_cat = desk_cat_initial(0xA1B2C3D4u);
  DeskCatState active_cat = desk_cat_initial(0xA1B2C3D4u);
  const DeskCatContext night_ctx = {RuntimeMode::Host, 23 * 60 + 30, false};
  const DeskCatContext active_ctx = {RuntimeMode::Host, 12 * 60 + 30, true};

  int night_sleep = 0;
  int night_play = 0;
  int active_sleep = 0;
  int active_play = 0;

  for (int i = 0; i < 1500; i++) {
    step_desk_cat(night_cat, night_ctx);
    step_desk_cat(active_cat, active_ctx);
    if (night_cat.mode == DeskCatMode::Sleep) night_sleep++;
    if (night_cat.mode == DeskCatMode::PlayYarn) night_play++;
    if (active_cat.mode == DeskCatMode::Sleep) active_sleep++;
    if (active_cat.mode == DeskCatMode::PlayYarn) active_play++;
  }

  TEST_ASSERT_GREATER_THAN(active_sleep + 200, night_sleep);
  TEST_ASSERT_EQUAL(0, night_play);
  TEST_ASSERT_GREATER_THAN(200, active_play);
}

void test_desk_cat_random_spawn_spreads_across_all_ledges_and_stays_in_bounds() {
  bool seen_ledge[kDeskCatLedgeCount] = {false, false, false};
  for (uint32_t seed = 1; seed <= 64; seed++) {
    const DeskCatState cat = desk_cat_random_spawn(seed * 2654435761u);
    TEST_ASSERT_LESS_THAN(kDeskCatLedgeCount, cat.ledge_idx);
    const DeskCatLedge& ledge = kDeskCatLedges[cat.ledge_idx];
    seen_ledge[cat.ledge_idx] = true;
    TEST_ASSERT_TRUE(cat.x >= (float)ledge.min_x && cat.x <= (float)ledge.max_x);
    TEST_ASSERT_EQUAL_FLOAT((float)ledge.y, cat.y);
    TEST_ASSERT_TRUE(cat.dir == 1 || cat.dir == -1);
    if (cat.yarn_active) {
      TEST_ASSERT_TRUE(cat.yarn_x >= (float)ledge.min_x &&
                       cat.yarn_x <= (float)ledge.max_x);
    }
  }
  for (size_t i = 0; i < kDeskCatLedgeCount; i++) {
    TEST_ASSERT_TRUE(seen_ledge[i]);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_desk_cat_stays_within_ledge_and_yarn_bounds_across_long_run);
  RUN_TEST(test_desk_cat_active_and_eco_never_mutate_state_and_render_static_pose);
  RUN_TEST(test_desk_cat_mood_weights_favor_sleep_at_night_and_play_on_active_days);
  RUN_TEST(test_desk_cat_random_spawn_spreads_across_all_ledges_and_stays_in_bounds);
  return UNITY_END();
}
