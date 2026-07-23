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

struct TextOrigin {
  int16_t x = 0, y = 0;
  TextOrigin() = default;
  TextOrigin(int16_t x_, int16_t y_) : x(x_), y(y_) {}
};

// Compute the device-independent text cursor after the renderer has measured
// the text and scaled its pad/dy values.
TextOrigin text_origin(Rect bounds, Align align, int16_t text_width,
                       int16_t pad, int16_t dy);

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

// `scale` (<=1.0) shrinks pads/gaps/spacers/bar+grid extents; the Measurer
// scales text/icon/image to match. Used by the fit-to-height pass; defaults to
// 1.0 for the unscaled layout and the native tests.
LaidOutNode run_layout(const LayoutNode& root, Rect viewport, const Measurer& m, float scale = 1.0f);

}  // namespace rcpaper
