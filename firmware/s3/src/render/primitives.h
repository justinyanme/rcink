#pragma once

#ifdef ARDUINO
#include <M5GFX.h>

#include "render/engine.h"

namespace rcpaper {

class M5GFXMeasurer : public Measurer {
 public:
  explicit M5GFXMeasurer(M5GFX& gfx) : gfx_(gfx) {}
  Size text_size(const TextNode& t) const override;
  Size icon_size(const IconNode& i) const override;
  Size image_size(const ImageNode& i) const override;

 private:
  M5GFX& gfx_;
};

void paint_layout(LovyanGFX& gfx, const LaidOutNode& laid);

}  // namespace rcpaper
#endif
