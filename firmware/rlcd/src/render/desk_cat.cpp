#include "render/desk_cat.h"

#include <algorithm>
#include <cmath>

namespace rcpaper {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float next_unit(DeskCatState& cat) {
  cat.rng_state = cat.rng_state * 1664525u + 1013904223u;
  return (float)(cat.rng_state >> 8) / 16777216.0f;  // [0, 1)
}

float clampf(float v, float lo, float hi) {
  return std::max(lo, std::min(hi, v));
}

void pick_next_mode(DeskCatState& cat, const DeskCatContext& ctx) {
  const uint16_t mins = (uint16_t)(ctx.minutes_of_day % 1440);
  const bool is_night = (mins >= 22 * 60 || mins < 6 * 60);
  const float r = next_unit(cat);

  if (r < 0.26f) {
    start_desk_cat_jump(cat);
    return;
  }

  const DeskCatLedge& ledge = kDeskCatLedges[cat.ledge_idx % kDeskCatLedgeCount];
  const float min_x = (float)ledge.min_x;
  const float max_x = (float)ledge.max_x;

  if (is_night) {
    if (r < 0.72f) {
      cat.mode = DeskCatMode::Sleep;
      cat.timer = (int16_t)(44 + (int)(next_unit(cat) * 30.0f));
      cat.yarn_active = false;
    } else {
      cat.mode = DeskCatMode::Sit;
      cat.timer = 24;
      cat.yarn_active = false;
    }
    return;
  }

  if (ctx.active_day) {
    if (r < 0.62f) {
      cat.mode = DeskCatMode::PlayYarn;
      cat.timer = (int16_t)(32 + (int)(next_unit(cat) * 26.0f));
      cat.yarn_active = true;
      cat.yarn_x = clampf(cat.x + (float)cat.dir * 26.0f, min_x, max_x);
      cat.yarn_vx = (float)cat.dir * (2.2f + next_unit(cat) * 1.4f);
    } else if (r < 0.88f) {
      cat.mode = DeskCatMode::Walk;
      cat.dir = next_unit(cat) < 0.5f ? 1 : -1;
      cat.timer = (int16_t)(22 + (int)(next_unit(cat) * 24.0f));
      cat.yarn_active = false;
    } else {
      cat.mode = DeskCatMode::Sit;
      cat.timer = 14;
      cat.yarn_active = false;
    }
    return;
  }

  if (r < 0.52f) {
    cat.mode = DeskCatMode::Walk;
    cat.dir = next_unit(cat) < 0.5f ? 1 : -1;
    cat.timer = 22;
    cat.yarn_active = false;
  } else if (r < 0.78f) {
    cat.mode = DeskCatMode::Sit;
    cat.timer = 22;
    cat.yarn_active = false;
  } else if (r < 0.90f) {
    cat.mode = DeskCatMode::PlayYarn;
    cat.timer = 28;
    cat.yarn_active = true;
    cat.yarn_x = clampf(cat.x + (float)cat.dir * 22.0f, min_x, max_x);
    cat.yarn_vx = (float)cat.dir * 2.0f;
  } else {
    cat.mode = DeskCatMode::Sleep;
    cat.timer = 32;
    cat.yarn_active = false;
  }
}

}  // namespace

DeskCatState desk_cat_initial(uint32_t seed) {
  DeskCatState cat;
  cat.ledge_idx = 1;
  cat.x = 110.0f;
  cat.y = (float)kDeskCatLedges[1].y;
  cat.dir = 1;
  cat.mode = DeskCatMode::Sit;
  cat.timer = 18;
  cat.yarn_active = false;
  cat.yarn_x = 136.0f;
  cat.yarn_vx = 2.2f;
  cat.rng_state = seed == 0 ? 0x51A7C471u : seed;
  return cat;
}

DeskCatState desk_cat_random_spawn(uint32_t seed) {
  DeskCatState cat = desk_cat_initial(seed);
  // Mix seed twice so nearby timestamps/seeds spread cleanly across ledges.
  (void)next_unit(cat);
  const size_t ledge_pick =
      (size_t)(next_unit(cat) * (float)kDeskCatLedgeCount);
  cat.ledge_idx = (uint8_t)(ledge_pick >= kDeskCatLedgeCount
                                ? kDeskCatLedgeCount - 1
                                : ledge_pick);
  const DeskCatLedge& ledge = kDeskCatLedges[cat.ledge_idx];
  const float span = (float)(ledge.max_x - ledge.min_x);
  cat.x = clampf(std::round((float)ledge.min_x + next_unit(cat) * span),
                 (float)ledge.min_x, (float)ledge.max_x);
  cat.y = (float)ledge.y;
  cat.dir = next_unit(cat) < 0.5f ? 1 : -1;
  const float mode_r = next_unit(cat);
  if (mode_r < 0.40f) {
    cat.mode = DeskCatMode::Walk;
    cat.timer = (int16_t)(16 + (int)(next_unit(cat) * 20.0f));
    cat.yarn_active = false;
  } else if (mode_r < 0.72f) {
    cat.mode = DeskCatMode::PlayYarn;
    cat.timer = (int16_t)(24 + (int)(next_unit(cat) * 24.0f));
    cat.yarn_active = true;
    cat.yarn_x = clampf(cat.x + (float)cat.dir * 22.0f, (float)ledge.min_x,
                        (float)ledge.max_x);
    cat.yarn_vx = (float)cat.dir * (2.0f + next_unit(cat) * 1.4f);
  } else {
    cat.mode = DeskCatMode::Sit;
    cat.timer = (int16_t)(12 + (int)(next_unit(cat) * 16.0f));
    cat.yarn_active = false;
  }
  return cat;
}

DeskCatState desk_cat_static_pose() {
  DeskCatState cat;
  cat.ledge_idx = 1;
  cat.x = 160.0f;
  cat.y = (float)kDeskCatLedges[1].y;
  cat.dir = 1;
  cat.mode = DeskCatMode::Sit;
  cat.timer = 0;
  cat.yarn_active = false;
  return cat;
}

DeskCatState desk_cat_render_pose(const DeskCatState& cat, bool animating) {
  if (!animating) return desk_cat_static_pose();
  return cat;
}

void start_desk_cat_jump(DeskCatState& cat, int8_t target_ledge) {
  const uint8_t cur = (uint8_t)(cat.ledge_idx % kDeskCatLedgeCount);
  uint8_t dest = cur;
  if (target_ledge >= 0 && (size_t)target_ledge < kDeskCatLedgeCount &&
      (uint8_t)target_ledge != cur) {
    dest = (uint8_t)target_ledge;
  } else {
    uint8_t choices[kDeskCatLedgeCount - 1];
    size_t n = 0;
    for (uint8_t i = 0; i < kDeskCatLedgeCount; i++) {
      if (i != cur) choices[n++] = i;
    }
    const size_t pick = (size_t)(next_unit(cat) * (float)n);
    dest = choices[pick >= n ? n - 1 : pick];
  }

  const DeskCatLedge& dst = kDeskCatLedges[dest];
  cat.mode = DeskCatMode::Jump;
  cat.yarn_active = false;
  cat.jump_t = 0;
  cat.jump_dur = kDeskCatJumpDuration;
  cat.j_start_x = cat.x;
  cat.j_start_y = cat.y;
  const float ideal_x = cat.x + (next_unit(cat) * 80.0f - 40.0f);
  cat.j_end_x = clampf(std::round(ideal_x), (float)dst.min_x, (float)dst.max_x);
  cat.j_end_y = (float)dst.y;
  cat.j_target_ledge = dest;
  cat.dir = cat.j_end_x >= cat.j_start_x ? 1 : -1;
}

void step_desk_cat(DeskCatState& cat, const DeskCatContext& ctx) {
  // Never mutate cat state in battery ACTIVE or ECO modes.
  if (ctx.runtime_mode != RuntimeMode::Host) return;

  if (cat.mode == DeskCatMode::Jump) {
    const uint8_t dur = cat.jump_dur == 0 ? kDeskCatJumpDuration : cat.jump_dur;
    cat.jump_t++;
    const float u = std::min(1.0f, (float)cat.jump_t / (float)dur);
    cat.x = cat.j_start_x + (cat.j_end_x - cat.j_start_x) * u;
    cat.y = cat.j_start_y + (cat.j_end_y - cat.j_start_y) * u -
            std::sin(u * kPi) * (float)kDeskCatPeakLift;
    if (cat.jump_t >= dur) {
      cat.ledge_idx = (uint8_t)(cat.j_target_ledge % kDeskCatLedgeCount);
      const DeskCatLedge& landed = kDeskCatLedges[cat.ledge_idx];
      cat.x = clampf(cat.j_end_x, (float)landed.min_x, (float)landed.max_x);
      cat.y = (float)landed.y;
      cat.mode = DeskCatMode::Sit;
      cat.timer = 8;
    }
    return;
  }

  cat.ledge_idx = (uint8_t)(cat.ledge_idx % kDeskCatLedgeCount);
  const DeskCatLedge& ledge = kDeskCatLedges[cat.ledge_idx];
  const float min_x = (float)ledge.min_x;
  const float max_x = (float)ledge.max_x;
  cat.y = (float)ledge.y;
  cat.x = clampf(cat.x, min_x, max_x);

  cat.timer--;
  if (cat.timer <= 0) {
    pick_next_mode(cat, ctx);
    return;
  }

  if (cat.mode == DeskCatMode::Walk) {
    cat.x += (float)cat.dir * 2.0f;
    if (cat.x <= min_x) {
      cat.x = min_x;
      cat.dir = 1;
    }
    if (cat.x >= max_x) {
      cat.x = max_x;
      cat.dir = -1;
    }
  } else if (cat.mode == DeskCatMode::PlayYarn) {
    cat.yarn_x += cat.yarn_vx;
    cat.yarn_vx *= 0.91f;
    if (cat.yarn_x <= min_x) {
      cat.yarn_x = min_x;
      cat.yarn_vx = std::fabs(cat.yarn_vx) + 1.5f;
    }
    if (cat.yarn_x >= max_x) {
      cat.yarn_x = max_x;
      cat.yarn_vx = -(std::fabs(cat.yarn_vx) + 1.5f);
    }
    const float target = cat.yarn_x - (cat.dir > 0 ? 15.0f : -5.0f);
    const float dx = target - cat.x;
    if (std::fabs(dx) > 3.0f) {
      cat.dir = dx > 0.0f ? 1 : -1;
      cat.x += (float)cat.dir * (std::fabs(dx) > 16.0f ? 3.0f : 2.0f);
    } else {
      cat.yarn_vx = (float)cat.dir * (3.0f + next_unit(cat) * 1.8f);
    }
    cat.x = clampf(cat.x, min_x, max_x);
  }
}

}  // namespace rcpaper
