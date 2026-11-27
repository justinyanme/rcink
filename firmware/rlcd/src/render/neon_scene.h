#pragma once

#include <array>
#include <cstdint>

#include "render/mono_pack.h"
#include "render/neon_view.h"

namespace rcpaper {

// Row-major, MSB first, 1 = black ink (binary PBM). Both polarities are final.
constexpr int kNeonWidth = 400, kNeonHeight = 300;
using NeonBitmap = std::array<uint8_t, kNeonWidth * kNeonHeight / 8>;

// A visible-state redraw (including entry, polarity or mode change) starts at
// frame zero. Only a subsequent quiet HOST tick can advance it.
struct NeonAnimation {
  uint32_t frame = 0;
  uint32_t last_frame_ms = 0;

  void restart(uint32_t now_ms) {
    frame = 0;
    last_frame_ms = now_ms;
  }
  bool advance(uint32_t now_ms, RuntimeMode mode, bool enabled) {
    if (mode != RuntimeMode::Host || !enabled || now_ms - last_frame_ms < 125) return false;
    ++frame;
    last_frame_ms = now_ms;
    return true;
  }
};

void render_neon_scene(const NeonView& view, MonoPolarity polarity,
                       uint32_t frame, NeonBitmap& out);

class St7305Frame;
bool render_neon(const NeonView& view, uint32_t frame, MonoPolarity polarity,
                 uint16_t* canvas, St7305Frame& panel);

}  // namespace rcpaper
