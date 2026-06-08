#ifdef ARDUINO

#include "render/primitives.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "render/fonts.h"
#include "render/palette.h"

namespace rcpaper {

static uint16_t rgb565(uint32_t rgb) {
  const uint8_t r = (rgb >> 16) & 0xFF;
  const uint8_t g = (rgb >> 8) & 0xFF;
  const uint8_t b = rgb & 0xFF;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t c565(Color c) {
  return color_to_rgb565(c);
}

// Truncate a UTF-8 string with an ellipsis so its rendered width fits `max_w`
// at (size, weight). Walks one codepoint at a time so multi-byte CJK is never
// split mid-character; width is measured CJK-aware via measured_width().
static std::string fit_text(TFT_eSprite& canvas, TextSize size, Weight weight,
                            const std::string& raw, int16_t max_w) {
  if (max_w <= 0) return raw;
  if (measured_width(canvas, size, weight, raw) <= max_w) return raw;
  const char* ell = "...";
  if (measured_width(canvas, size, weight, ell) >= max_w) return {};
  std::string out;
  for (size_t i = 0; i < raw.size();) {
    const unsigned char c = (unsigned char)raw[i];
    size_t step = (c < 0x80) ? 1 : (c < 0xC0) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
    if (i + step > raw.size()) step = raw.size() - i;
    if (measured_width(canvas, size, weight, out + raw.substr(i, step) + ell) > max_w) break;
    out.append(raw, i, step);
    i += step;
  }
  return out + ell;
}

void draw_text(TFT_eSprite& canvas, const TextNode& t, Rect r) {
  if (r.w <= 0 || r.h <= 0) return;
  if (t.has_bg) canvas.fillRect(r.x, r.y, r.w, r.h, c565(t.bg));

  const int16_t pad = scale_px(t.pad);
  int16_t inner_w = r.w - 2 * pad;
  if (inner_w < 0) inner_w = 0;
  const std::string fitted = fit_text(canvas, t.size, t.weight, t.value, inner_w);
  const int16_t text_w = measured_width(canvas, t.size, t.weight, fitted);

  int16_t tx = r.x + pad;
  if (t.align == Align::Center) tx = r.x + (r.w - text_w) / 2;
  if (t.align == Align::End) tx = r.x + r.w - text_w - pad;
  const int16_t ty = r.y + pad;
  const uint16_t bg = t.has_bg ? c565(t.bg) : rgb565(kRgbWhite);
  draw_utf8(canvas, tx, ty, t.size, t.weight, c565(t.color), t.has_bg, bg, fitted);
}

void draw_divider(TFT_eSprite& canvas, const DividerNode& d, Rect r) {
  if (r.w <= 0 || r.h <= 0) return;
  canvas.fillRect(r.x, r.y, r.w, std::max<int16_t>(1, d.thickness), c565(d.color));
}

void draw_hbar(TFT_eSprite& canvas, const HBarNode& b, Rect r) {
  if (r.w <= 4 || r.h <= 4) return;
  const uint16_t fg = c565(b.color);
  canvas.drawRect(r.x, r.y, r.w, r.h, c565(Color::Black));
  int16_t pct = (int16_t)b.pct;
  pct = std::max<int16_t>(0, std::min<int16_t>(100, pct));
  const int16_t inner_w = r.w - 4;
  const int16_t fill = (int16_t)(((int32_t)inner_w * pct) / 100);
  if (fill > 0) canvas.fillRect(r.x + 2, r.y + 2, fill, r.h - 4, fg);
  if (!b.label_left.empty()) {
    const int16_t lh = string_needs_cjk(b.label_left) ? kCjkLineH : 8;
    draw_utf8(canvas, r.x + 4, r.y + (r.h - lh) / 2, TextSize::Sm, Weight::Regular,
              rgb565(kRgbWhite), true, fg, b.label_left);
  }
  if (!b.label_right.empty()) {
    const int16_t lh = string_needs_cjk(b.label_right) ? kCjkLineH : 8;
    const int16_t tw = measured_width(canvas, TextSize::Sm, Weight::Regular, b.label_right);
    draw_utf8(canvas, r.x + r.w - tw - 4, r.y + (r.h - lh) / 2, TextSize::Sm,
              Weight::Regular, c565(Color::Black), false, rgb565(kRgbWhite), b.label_right);
  }
}

void draw_marker(TFT_eSprite& canvas, const MarkerNode& m, Rect r) {
  int16_t pct = (int16_t)m.pct;
  pct = std::max<int16_t>(0, std::min<int16_t>(100, pct));
  const int16_t x = r.x + (int16_t)(((int32_t)r.w * pct) / 100);
  canvas.drawFastVLine(x, r.y - 4, r.h + 8, c565(m.color));
}

void draw_icon(TFT_eSprite& canvas, const IconNode& i, Rect r) {
  // Clamp to the (already fit-scaled) rect so the glyph box never overflows.
  const int16_t s = std::min<int16_t>(i.size > 0 ? i.size : 16, std::min(r.w, r.h));
  if (s <= 0) return;
  const int16_t x = r.x + (r.w - s) / 2;
  const int16_t y = r.y + (r.h - s) / 2;
  canvas.fillRect(x, y, s, s, c565(i.color));
  canvas.drawRect(x, y, s, s, c565(Color::Black));
  if (!i.name.empty() && s >= 12) {
    canvas.setTextFont(1);
    canvas.setTextSize(1);
    canvas.setTextColor(rgb565(kRgbWhite), c565(i.color));
    canvas.setCursor(x + 3, y + (s - 8) / 2);
    canvas.print((char)std::toupper((unsigned char)i.name[0]));
  }
}

void draw_image(TFT_eSprite& canvas, const ImageNode& i, Rect r) {
  if (r.w <= 0 || r.h <= 0) return;
  canvas.drawRect(r.x, r.y, r.w, r.h, c565(Color::Black));
  canvas.setTextFont(1);
  canvas.setTextSize(1);
  canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));
  canvas.setCursor(r.x + 4, r.y + 4);
  canvas.print("IMAGE");
  const std::string label = !i.name.empty() ? i.name : i.url;
  const std::string fitted = fit_text(canvas, TextSize::Sm, Weight::Regular, label, r.w - 8);
  draw_utf8(canvas, r.x + 4, r.y + 16, TextSize::Sm, Weight::Regular,
            c565(Color::Black), false, rgb565(kRgbWhite), fitted);
}

void draw_sparkline(TFT_eSprite& canvas, const SparklineNode& s, Rect r) {
  if (s.values.empty() || r.w <= 1 || r.h <= 1) return;
  const uint16_t fg = c565(s.color);
  float hi = s.values[0];
  for (float v : s.values) hi = std::max(hi, v);
  if (hi <= 0) hi = 1;
  const int16_t gutter = 2;
  const int16_t inner_h = r.h - 2 * gutter;
  auto y_of = [&](float v) -> int16_t {
    v = std::max(0.0f, std::min(hi, v));
    return (int16_t)(r.y + gutter + inner_h - (int16_t)((v / hi) * inner_h));
  };
  const size_t n = s.values.size();
  for (size_t i = 1; i < n; ++i) {
    const int16_t x0 = r.x + (int16_t)(((int32_t)(i - 1) * (r.w - 1)) / (int32_t)(n - 1));
    const int16_t x1 = r.x + (int16_t)(((int32_t)i * (r.w - 1)) / (int32_t)(n - 1));
    canvas.drawLine(x0, y_of(s.values[i - 1]), x1, y_of(s.values[i]), fg);
  }
  for (int16_t x = r.x; x < r.x + r.w; x += 4) {
    canvas.drawPixel(x, r.y + r.h - 1, c565(Color::Black));
  }
}

void draw_grid(TFT_eSprite& canvas, const GridNode& g, Rect r) {
  if (g.cols <= 0 || g.rows <= 0) return;
  const int16_t avail_w = r.w - (g.cols - 1) * g.gap;
  const int16_t avail_h = r.h - (g.rows - 1) * g.gap;
  if (avail_w <= 0 || avail_h <= 0) return;
  const int16_t cell = std::min<int16_t>(avail_w / g.cols, avail_h / g.rows);
  if (cell <= 0) return;
  const int16_t total_w = g.cols * cell + (g.cols - 1) * g.gap;
  const int16_t total_h = g.rows * cell + (g.rows - 1) * g.gap;
  const int16_t ox = r.x + (r.w - total_w) / 2;
  const int16_t oy = r.y + (r.h - total_h) / 2;
  auto ink_for = [&](int16_t v) {
    if (v <= 0) return c565(Color::White);
    if (v < 30) return c565(Color::Yellow);
    if (v < 65) return c565(Color::Green);
    if (v < 90) return c565(Color::Blue);
    return c565(Color::Red);
  };
  for (int16_t col = 0; col < g.cols; ++col) {
    for (int16_t row = 0; row < g.rows; ++row) {
      const size_t idx = (size_t)col * g.rows + row;
      const int16_t v = idx < g.values.size() ? g.values[idx] : 0;
      const int16_t x = ox + col * (cell + g.gap);
      const int16_t y = oy + row * (cell + g.gap);
      canvas.fillRect(x, y, cell, cell, ink_for(v));
      canvas.drawRect(x, y, cell, cell, c565(Color::Black));
    }
  }
}

static void paint_node(TFT_eSprite& canvas, const LaidOutNode& laid) {
  if (!laid.node) return;
  switch (laid.node->type) {
    case NodeType::VStack:
    case NodeType::HStack:
      for (const auto& c : laid.children) paint_node(canvas, c);
      break;
    case NodeType::Spacer:
      break;
    case NodeType::Divider:   draw_divider(canvas, laid.node->as_divider, laid.rect); break;
    case NodeType::Text:      draw_text(canvas, laid.node->as_text, laid.rect); break;
    case NodeType::HBar:      draw_hbar(canvas, laid.node->as_hbar, laid.rect); break;
    case NodeType::Icon:      draw_icon(canvas, laid.node->as_icon, laid.rect); break;
    case NodeType::Image:     draw_image(canvas, laid.node->as_image, laid.rect); break;
    case NodeType::Sparkline: draw_sparkline(canvas, laid.node->as_sparkline, laid.rect); break;
    case NodeType::Marker:    draw_marker(canvas, laid.node->as_marker, laid.rect); break;
    case NodeType::Grid:      draw_grid(canvas, laid.node->as_grid, laid.rect); break;
    case NodeType::Unknown:
    default:
      break;
  }
}

void paint_layout(TFT_eSprite& canvas, const LaidOutNode& laid) {
  paint_node(canvas, laid);
}

}  // namespace rcpaper

#endif  // ARDUINO
