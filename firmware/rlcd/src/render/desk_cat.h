#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "power/runtime_policy.h"

namespace rcpaper {

constexpr int16_t kDeskCatW = 15;
constexpr int16_t kDeskCatH = 10;
constexpr int16_t kDeskCatYarnSize = 5;
constexpr uint8_t kDeskCatJumpDuration = 8;  // 1.0 s at 8 FPS (125 ms/frame)
constexpr int16_t kDeskCatPeakLift = 26;
constexpr size_t kDeskCatLedgeCount = 3;

struct DeskCatLedge {
  int16_t y;
  int16_t min_x;
  int16_t max_x;
};

// Three collision-free natural horizontal divider ledges on Station Page.
constexpr std::array<DeskCatLedge, kDeskCatLedgeCount> kDeskCatLedges = {{
    {28, 98, 270},   // Ledge 0: Header rule (between "STATION" and Ambient Chrome)
    {151, 16, 344},  // Ledge 1: Spine divider (below Heatmap/Skyline, left of "7D")
    {272, 16, 368},  // Ledge 2: Footer rule (below 4th Provider row)
}};

enum class DeskCatMode : uint8_t {
  Walk = 0,
  Sit = 1,
  PlayYarn = 2,
  Sleep = 3,
  Jump = 4,
};

struct DeskCatContext {
  RuntimeMode runtime_mode = RuntimeMode::Host;
  uint16_t minutes_of_day = 750;  // 0..1439 local clock minutes
  bool active_day = false;        // true when today's Token Usage is active
};

struct DeskCatState {
  uint8_t ledge_idx = 1;
  float x = 110.0f;
  float y = 151.0f;
  int8_t dir = 1;
  DeskCatMode mode = DeskCatMode::Sit;
  int16_t timer = 20;
  bool yarn_active = false;
  float yarn_x = 136.0f;
  float yarn_vx = 2.2f;
  uint8_t jump_t = 0;
  uint8_t jump_dur = kDeskCatJumpDuration;
  float j_start_x = 110.0f;
  float j_start_y = 151.0f;
  float j_end_x = 110.0f;
  float j_end_y = 151.0f;
  uint8_t j_target_ledge = 1;
  uint32_t rng_state = 0x51A7C471u;
};

DeskCatState desk_cat_initial(uint32_t seed = 0x51A7C471u);
DeskCatState desk_cat_random_spawn(uint32_t seed);
DeskCatState desk_cat_static_pose();
DeskCatState desk_cat_render_pose(const DeskCatState& cat, bool animating);
void start_desk_cat_jump(DeskCatState& cat, int8_t target_ledge = -1);
void step_desk_cat(DeskCatState& cat, const DeskCatContext& ctx);

}  // namespace rcpaper
