#ifdef ARDUINO
#include <M5GFX.h>
#include "render/engine.h"
#include "render/fonts_icons.h"
#include "render/palette.h"
#include "proto/schema.h"

namespace rcpaper {

void draw_text(M5GFX& gfx, const TextNode& t, Rect r) {
  if (t.has_bg) {
    gfx.fillRect(r.x, r.y, r.w, r.h, color_to_m5gfx(t.bg));
  }
  gfx.setFont(font_for(t.size, t.weight));
  gfx.setTextColor(color_to_m5gfx(t.color));

  int16_t tx = r.x + t.pad;
  int16_t ty = r.y + t.pad;
  int16_t text_w = gfx.textWidth(t.value.c_str());

  if (t.align == Align::Center) tx = r.x + (r.w - text_w) / 2;
  else if (t.align == Align::End) tx = r.x + r.w - text_w - t.pad;

  gfx.setCursor(tx, ty);
  gfx.print(t.value.c_str());
}

void draw_divider(M5GFX& gfx, const DividerNode& d, Rect r, bool /*vertical_main*/) {
  gfx.fillRect(r.x, r.y, r.w, d.thickness, color_to_m5gfx(d.color));
}

// Stubs to be implemented in Tasks 7-8
void draw_hbar(M5GFX&, const HBarNode&, Rect) {}
void draw_icon(M5GFX&, const IconNode&, Rect) {}
void draw_image(M5GFX&, const ImageNode&, Rect) {}
void draw_sparkline(M5GFX&, const SparklineNode&, Rect) {}
void draw_marker(M5GFX&, const MarkerNode&, Rect) {}

}  // namespace rcpaper
#endif
