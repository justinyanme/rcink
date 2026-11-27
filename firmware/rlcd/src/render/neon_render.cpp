#include "render/neon_scene.h"
#include "display/st7305_frame.h"
#ifdef ARDUINO
#include "display/st7305.h"
#include "render/canvas.h"
#include "render/page.h"
#endif

namespace rcpaper {

bool render_neon(const NeonView& view, uint32_t frame, MonoPolarity polarity,
                 uint16_t* pixels, St7305Frame& panel) {
  if (!pixels) return false;
  static NeonBitmap bits;
  render_neon_scene(view, polarity, frame, bits);
  if (frame != 0) {
    panel.push_changes(bits.data());
    return true;
  }
  auto* dst = pixels;
  for (uint8_t byte : bits)
    for (int mask = 0x80; mask; mask >>= 1) *dst++ = byte & mask ? 0 : 0xffff;
  // The scene has already drawn the selected polarity into the sprite.
  panel.push(pixels, MonoPolarity::Light);
  return true;
}

#ifdef ARDUINO
bool render_neon(const NeonView& view, uint32_t frame) {
  auto* canvas = display_canvas();
  return render_neon(view, frame, st7305_dark_mode() ? MonoPolarity::Dark : MonoPolarity::Light,
                     canvas ? static_cast<uint16_t*>(canvas->getPointer()) : nullptr,
                     st7305_frame());
}
#endif

}  // namespace rcpaper
