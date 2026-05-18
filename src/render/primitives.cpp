#ifdef ARDUINO
#include <algorithm>
#include <M5GFX.h>
#include "render/engine.h"
#include "render/fonts_icons.h"
#include "render/palette.h"
#include "proto/schema.h"

namespace rcpaper {

void draw_text(LovyanGFX& gfx, const TextNode& t, Rect r) {
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

void draw_divider(LovyanGFX& gfx, const DividerNode& d, Rect r, bool /*vertical_main*/) {
  gfx.fillRect(r.x, r.y, r.w, d.thickness, color_to_m5gfx(d.color));
}

void draw_hbar(LovyanGFX& gfx, const HBarNode& b, Rect r) {
  // Border
  uint32_t fg = color_to_m5gfx(b.color);
  gfx.drawRect(r.x, r.y, r.w, r.h, 0x000000);
  // Fill
  int16_t inner_w = r.w - 4;
  int16_t pct = (int16_t)b.pct;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int16_t fill = (int16_t)(((int32_t)inner_w * pct) / 100);
  if (fill > 0) gfx.fillRect(r.x + 2, r.y + 2, fill, r.h - 4, fg);

  // Optional labels — left at the inset, right at the trailing edge.
  if (!b.label_left.empty()) {
    gfx.setFont(font_for(TextSize::Sm, Weight::Bold));
    gfx.setTextColor(0xFFFFFF);
    gfx.setCursor(r.x + 4, r.y + (r.h - 14) / 2);
    gfx.print(b.label_left.c_str());
  }
  if (!b.label_right.empty()) {
    gfx.setFont(font_for(TextSize::Sm, Weight::Bold));
    gfx.setTextColor(0x000000);
    int16_t tw = gfx.textWidth(b.label_right.c_str());
    gfx.setCursor(r.x + r.w - tw - 4, r.y + (r.h - 14) / 2);
    gfx.print(b.label_right.c_str());
  }
}

void draw_marker(LovyanGFX& gfx, const MarkerNode& m, Rect r) {
  // r covers the previous sibling's rect (passed by engine via overlay layout).
  // We just draw a vertical line at pct of r.w, full height of r.
  int16_t pct = (int16_t)m.pct;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int16_t x = r.x + (int16_t)(((int32_t)r.w * pct) / 100);
  gfx.drawFastVLine(x, r.y - 4, r.h + 8, color_to_m5gfx(m.color));
}

void draw_sparkline(LovyanGFX& gfx, const SparklineNode& s, Rect r) {
  if (s.values.empty()) return;
  uint32_t fg = color_to_m5gfx(s.color);

  // Baseline + range
  float lo = s.values[0], hi = s.values[0];
  for (float v : s.values) { if (v < lo) lo = v; if (v > hi) hi = v; }
  if (hi == lo) { lo -= 1; hi += 1; }

  auto y_of = [&](float v) -> int16_t {
    float pct = (v - lo) / (hi - lo);
    return (int16_t)(r.y + r.h - (int16_t)(pct * (r.h - 2)) - 1);
  };

  size_t n = s.values.size();
  if (n < 2) {
    gfx.fillCircle(r.x + r.w / 2, y_of(s.values[0]), 2, fg);
    return;
  }
  for (size_t i = 1; i < n; ++i) {
    int16_t x0 = r.x + (int16_t)(((int32_t)(i - 1) * (r.w - 1)) / (int32_t)(n - 1));
    int16_t x1 = r.x + (int16_t)(((int32_t)i       * (r.w - 1)) / (int32_t)(n - 1));
    gfx.drawLine(x0, y_of(s.values[i - 1]), x1, y_of(s.values[i]), fg);
  }
  // Baseline rule
  gfx.drawFastHLine(r.x, r.y + r.h - 1, r.w, 0x000000);
}

void draw_icon(LovyanGFX& gfx, const IconNode& i, Rect r) {
  // Placeholder: filled square of the requested size + colour, centred in rect.
  int16_t s = i.size > 0 ? i.size : (int16_t)std::min(r.w, r.h);
  int16_t x = r.x + (r.w - s) / 2;
  int16_t y = r.y + (r.h - s) / 2;
  gfx.fillRect(x, y, s, s, color_to_m5gfx(i.color));
}

void draw_image(LovyanGFX& gfx, const ImageNode& i, Rect r) {
  // v1 stub: outline the area + label.
  gfx.drawRect(r.x, r.y, r.w, r.h, 0x000000);
  gfx.setFont(font_for(TextSize::Sm, Weight::Regular));
  gfx.setTextColor(0x000000);
  gfx.setCursor(r.x + 4, r.y + 4);
  gfx.printf("img:%s", i.name.c_str());
}

}  // namespace rcpaper
#endif
