#ifdef ARDUINO
#include <algorithm>
#include <M5GFX.h>
#include "render/engine.h"
#include "render/fonts_icons.h"
#include "render/palette.h"
#include "proto/schema.h"

namespace rcpaper {

// Truncate a UTF-8 string with an ellipsis so its rendered width fits inside
// `max_w` using the currently configured font. We assume `gfx` already has
// the right font/size set; the caller swaps in the CJK font as needed.
static std::string fit_utf8(LovyanGFX& gfx, const std::string& s, int16_t max_w) {
  if (max_w <= 0) return s;
  if (gfx.textWidth(s.c_str()) <= max_w) return s;
  const char* ell = "...";
  int16_t ell_w = gfx.textWidth(ell);
  if (ell_w >= max_w) {
    // Almost no room — return empty so we don't draw a stray "..." in 1 px.
    return std::string();
  }
  // Walk one codepoint at a time. UTF-8 lead bytes start with 0xxxxxxx or
  // 11xxxxxx; continuation bytes are 10xxxxxx.
  std::string out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    size_t step = (c < 0x80) ? 1
                : (c < 0xC0) ? 1                 // stray cont byte; skip 1
                : (c < 0xE0) ? 2
                : (c < 0xF0) ? 3
                :              4;
    if (i + step > s.size()) step = s.size() - i;
    std::string candidate = out + s.substr(i, step) + ell;
    if (gfx.textWidth(candidate.c_str()) > max_w) break;
    out.append(s, i, step);
    i += step;
  }
  return out + ell;
}

void draw_text(LovyanGFX& gfx, const TextNode& t, Rect r) {
  if (t.has_bg) {
    gfx.fillRect(r.x, r.y, r.w, r.h, color_to_m5gfx(t.bg));
  }
  // Wrap is on by default; that breaks our fixed-height row layout because
  // wrapped text writes onto the next row's pixels without claiming extra
  // vertical space. Disable wrap and instead truncate with an ellipsis below.
  gfx.setTextWrap(false, false);
  const bool needs_cjk = string_needs_cjk(t.value);
  gfx.setFont(needs_cjk ? cjk_font_for(t.size, t.weight)
                        : font_for(t.size, t.weight));
  gfx.setTextColor(color_to_m5gfx(t.color));

  int16_t inner_w = r.w - 2 * t.pad;
  if (inner_w < 0) inner_w = 0;
  const std::string fitted = fit_utf8(gfx, t.value, inner_w);

  int16_t tx = r.x + t.pad;
  int16_t ty = r.y + t.pad;
  int16_t text_w = gfx.textWidth(fitted.c_str());

  if (t.align == Align::Center) tx = r.x + (r.w - text_w) / 2;
  else if (t.align == Align::End) tx = r.x + r.w - text_w - t.pad;

  gfx.setCursor(tx, ty);
  gfx.print(fitted.c_str());
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

  // Range. Anchor low at 0 so a flat line at 50% reads as half-height instead
  // of being normalised to a meaningless mid-line. Cap the top so 100%-bound
  // sparklines (usage percentages) can't get rescaled to a tiny range when
  // all values cluster.
  float hi = s.values[0];
  for (float v : s.values) { if (v > hi) hi = v; }
  if (hi <= 0) hi = 1;  // avoid div-by-zero

  // Reserve a 1-px gutter top + bottom so the line doesn't hug the edges.
  const int16_t gutter = 2;
  const int16_t inner_h = r.h - 2 * gutter;
  auto y_of = [&](float v) -> int16_t {
    if (v < 0) v = 0;
    if (v > hi) v = hi;
    float pct = v / hi;
    return (int16_t)(r.y + gutter + inner_h - (int16_t)(pct * inner_h));
  };

  size_t n = s.values.size();
  if (n < 2) {
    gfx.fillCircle(r.x + r.w / 2, y_of(s.values[0]), 2, fg);
    return;
  }
  // Thicker stroke (3 px) so the line is readable on the E6 panel; the M5GFX
  // antialiased line is only 1 px wide and tends to disappear at our colors.
  for (size_t i = 1; i < n; ++i) {
    int16_t x0 = r.x + (int16_t)(((int32_t)(i - 1) * (r.w - 1)) / (int32_t)(n - 1));
    int16_t x1 = r.x + (int16_t)(((int32_t)i       * (r.w - 1)) / (int32_t)(n - 1));
    int16_t y0 = y_of(s.values[i - 1]);
    int16_t y1 = y_of(s.values[i]);
    gfx.drawLine(x0, y0,     x1, y1,     fg);
    gfx.drawLine(x0, y0 - 1, x1, y1 - 1, fg);
    gfx.drawLine(x0, y0 + 1, x1, y1 + 1, fg);
  }
  // Dotted baseline at the bottom — visually anchors the chart without
  // competing with the data line.
  for (int16_t x = r.x; x < r.x + r.w; x += 4) {
    gfx.drawPixel(x, r.y + r.h - 1, 0x808080);
  }
}

namespace {

// Vector glyphs drawn procedurally so the firmware doesn't need bitmap assets.
// All glyphs draw into an s×s box at top-left (x,y) using the requested color.
// Stroke widths are tuned for 12–16 px icons on the E6 panel.

inline void icon_claude(LovyanGFX& g, int16_t x, int16_t y, int16_t s, uint32_t fg) {
  // 8-pointed burst: four bars rotated 45°. Brand-adjacent without a logo trace.
  int16_t cx = x + s / 2, cy = y + s / 2;
  int16_t arm = s / 2;
  int16_t t = std::max<int16_t>(1, s / 8);
  // Horizontal + vertical
  g.fillRect(cx - arm, cy - t / 2, arm * 2, t, fg);
  g.fillRect(cx - t / 2, cy - arm, t, arm * 2, fg);
  // Diagonals: drawn as thick lines
  for (int16_t i = -t / 2; i <= t / 2; ++i) {
    g.drawLine(cx - arm + i, cy - arm, cx + arm + i, cy + arm, fg);
    g.drawLine(cx - arm + i, cy + arm, cx + arm + i, cy - arm, fg);
  }
}

inline void icon_codex(LovyanGFX& g, int16_t x, int16_t y, int16_t s, uint32_t fg) {
  // "</>" — two angle brackets pointing outward.
  int16_t t = std::max<int16_t>(1, s / 8);
  int16_t lx = x + s / 6;
  int16_t rx = x + s - s / 6;
  int16_t top = y + s / 4;
  int16_t mid = y + s / 2;
  int16_t bot = y + s - s / 4;
  for (int16_t i = 0; i < t; ++i) {
    g.drawLine(lx, mid - i, lx + s / 3, top - i, fg);
    g.drawLine(lx, mid + i, lx + s / 3, bot + i, fg);
    g.drawLine(rx, mid - i, rx - s / 3, top - i, fg);
    g.drawLine(rx, mid + i, rx - s / 3, bot + i, fg);
  }
}

inline void icon_quote(LovyanGFX& g, int16_t x, int16_t y, int16_t s, uint32_t fg) {
  // Two filled "drop" shapes for an opening double quote.
  int16_t w = s * 2 / 5;
  int16_t h = s * 2 / 3;
  int16_t gap = std::max<int16_t>(1, s / 10);
  int16_t x1 = x + (s - 2 * w - gap) / 2;
  int16_t x2 = x1 + w + gap;
  int16_t top = y + s / 6;
  // Two filled rounded rects approximating quote shapes
  g.fillRect(x1, top, w, h, fg);
  g.fillRect(x2, top, w, h, fg);
  // Lower-right "tail" — small triangles below each blob
  g.fillTriangle(x1, top + h, x1 + w, top + h, x1 + w, top + h + h / 3, fg);
  g.fillTriangle(x2, top + h, x2 + w, top + h, x2 + w, top + h + h / 3, fg);
}

inline void icon_calendar(LovyanGFX& g, int16_t x, int16_t y, int16_t s, uint32_t fg) {
  // Rectangle with a thick header bar + two grid notches.
  int16_t t = std::max<int16_t>(1, s / 10);
  int16_t pad = s / 8;
  int16_t bx = x + pad, by = y + pad;
  int16_t bw = s - pad * 2, bh = s - pad * 2;
  // Outline
  for (int16_t i = 0; i < t; ++i) g.drawRect(bx + i, by + i, bw - 2 * i, bh - 2 * i, fg);
  // Header bar fill
  g.fillRect(bx, by, bw, std::max<int16_t>(2, bh / 4), fg);
  // Two ring binders on top
  int16_t ring_w = std::max<int16_t>(1, bw / 8);
  int16_t ring_h = std::max<int16_t>(2, bh / 5);
  g.fillRect(bx + bw / 4 - ring_w / 2, by - ring_h / 2, ring_w, ring_h, fg);
  g.fillRect(bx + 3 * bw / 4 - ring_w / 2, by - ring_h / 2, ring_w, ring_h, fg);
}

inline void icon_todo(LovyanGFX& g, int16_t x, int16_t y, int16_t s, uint32_t fg) {
  // Checkbox: outlined square with a check mark inside.
  int16_t t = std::max<int16_t>(1, s / 10);
  int16_t pad = s / 8;
  int16_t bx = x + pad, by = y + pad;
  int16_t bw = s - pad * 2, bh = s - pad * 2;
  for (int16_t i = 0; i < t; ++i) g.drawRect(bx + i, by + i, bw - 2 * i, bh - 2 * i, fg);
  // Check mark: 2 line segments
  int16_t cw = std::max<int16_t>(1, s / 7);
  for (int16_t i = -cw / 2; i <= cw / 2; ++i) {
    g.drawLine(bx + bw / 5, by + bh / 2 + i, bx + bw * 2 / 5, by + bh * 3 / 4 + i, fg);
    g.drawLine(bx + bw * 2 / 5, by + bh * 3 / 4 + i, bx + bw * 4 / 5, by + bh / 4 + i, fg);
  }
}

inline void icon_weather(LovyanGFX& g, int16_t x, int16_t y, int16_t s, uint32_t fg) {
  // Sun: filled disc with 8 short rays.
  int16_t cx = x + s / 2, cy = y + s / 2;
  int16_t rd = s / 4;
  int16_t ray = s / 2 - rd / 2;
  g.fillCircle(cx, cy, rd, fg);
  int16_t t = std::max<int16_t>(1, s / 12);
  // 4 cardinal rays
  g.fillRect(cx - t / 2, cy - rd - ray, t, ray - 1, fg);
  g.fillRect(cx - t / 2, cy + rd + 1,   t, ray - 1, fg);
  g.fillRect(cx - rd - ray, cy - t / 2, ray - 1, t, fg);
  g.fillRect(cx + rd + 1,   cy - t / 2, ray - 1, t, fg);
  // 4 diagonal rays (drawn as short thick lines)
  int16_t d = (int16_t)(rd * 0.8f);
  for (int16_t i = -t / 2; i <= t / 2; ++i) {
    g.drawLine(cx + d + i, cy - d, cx + d + ray / 2 + i, cy - d - ray / 2, fg);
    g.drawLine(cx - d + i, cy - d, cx - d - ray / 2 + i, cy - d - ray / 2, fg);
    g.drawLine(cx + d + i, cy + d, cx + d + ray / 2 + i, cy + d + ray / 2, fg);
    g.drawLine(cx - d + i, cy + d, cx - d - ray / 2 + i, cy + d + ray / 2, fg);
  }
}

inline void icon_fallback(LovyanGFX& g, int16_t x, int16_t y, int16_t s, uint32_t fg) {
  // Filled square — preserves the v1 placeholder behavior for unknown names.
  g.fillRect(x, y, s, s, fg);
}

}  // namespace

void draw_icon(LovyanGFX& gfx, const IconNode& i, Rect r) {
  int16_t s = i.size > 0 ? i.size : (int16_t)std::min(r.w, r.h);
  int16_t x = r.x + (r.w - s) / 2;
  int16_t y = r.y + (r.h - s) / 2;
  uint32_t fg = color_to_m5gfx(i.color);

  const std::string& n = i.name;
  if      (n == "claude")   icon_claude(gfx, x, y, s, fg);
  else if (n == "codex")    icon_codex(gfx, x, y, s, fg);
  else if (n == "quote")    icon_quote(gfx, x, y, s, fg);
  else if (n == "calendar") icon_calendar(gfx, x, y, s, fg);
  else if (n == "todo")     icon_todo(gfx, x, y, s, fg);
  else if (n == "weather" || n == "sun") icon_weather(gfx, x, y, s, fg);
  else                      icon_fallback(gfx, x, y, s, fg);
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
