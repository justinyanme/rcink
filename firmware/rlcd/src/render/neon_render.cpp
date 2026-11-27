#ifdef ARDUINO
#include "render/canvas.h"
#include "display/st7305.h"
#include "render/neon_scene.h"
#include "render/page.h"

namespace rcpaper {

void render_neon(const NeonView& view, uint32_t frame) {
  auto* canvas = display_canvas();
  if (!canvas) return;
  static NeonBitmap bits;
  render_neon_scene(view, st7305_dark_mode() ? MonoPolarity::Dark : MonoPolarity::Light,
                    frame, bits);
  auto* pixels = static_cast<uint16_t*>(canvas->getPointer());
  auto* dst = pixels;
  for (uint8_t byte : bits)
    for (int mask = 0x80; mask; mask >>= 1) *dst++ = byte & mask ? 0 : 0xffff;
  // The scene has already drawn the selected polarity into the sprite.
  st7305_push_mono(pixels, kNeonWidth, kNeonHeight, MonoPolarity::Light);
}

}  // namespace rcpaper
#endif
