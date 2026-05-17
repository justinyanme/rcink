#pragma once
#include "proto/schema.h"
#include <cstdint>
#include <vector>

namespace rcpaper {

struct Size {
  int16_t w = 0, h = 0;
  Size() = default;
  Size(int16_t w_, int16_t h_) : w(w_), h(h_) {}
};
struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;
  Rect() = default;
  Rect(int16_t x_, int16_t y_, int16_t w_, int16_t h_) : x(x_), y(y_), w(w_), h(h_) {}
};

// Pluggable measurer (device supplies M5GFX impl; tests supply a stub).
struct Measurer {
  virtual ~Measurer() = default;
  virtual Size text_size(const TextNode&)   const = 0;
  virtual Size icon_size(const IconNode&)   const = 0;
  virtual Size image_size(const ImageNode&) const = 0;
};

struct LaidOutNode {
  const LayoutNode*        node = nullptr;
  Rect                     rect;
  std::vector<LaidOutNode> children;
};

LaidOutNode run_layout(const LayoutNode& root, Rect viewport, const Measurer& m);

}  // namespace rcpaper
