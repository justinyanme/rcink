#pragma once
#include "proto/schema.h"
#include <cstdint>
#include <vector>
#ifdef ARDUINO
#include <M5GFX.h>
#endif

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

#ifdef ARDUINO
class M5GFXMeasurer : public Measurer {
 public:
  explicit M5GFXMeasurer(M5GFX& gfx) : gfx_(gfx) {}
  Size text_size(const TextNode& t) const override;
  Size icon_size(const IconNode& i) const override;
  Size image_size(const ImageNode& i) const override;
 private:
  M5GFX& gfx_;
};

// Accepts a LovyanGFX surface (M5.Display *or* an M5Canvas sprite) so the
// caller can draw to an offscreen buffer and push once — color e-paper
// panels commit per-call on the live framebuffer, so direct-draw shows up
// as a refresh-per-element.
void paint_layout(LovyanGFX& gfx, const LaidOutNode& laid);
#endif

}  // namespace rcpaper
