#ifdef ARDUINO
#include <algorithm>
#include <cctype>
#include <cstring>
#include <HTTPClient.h>
#include <M5GFX.h>
#include "render/engine.h"
#include "render/fonts_icons.h"
#include "render/icons_data.h"
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

// Look up a baked glyph by its DSL name (claude, codex, calendar, ...).
const icons::Glyph* find_glyph(const std::string& name) {
  for (const auto* p = icons::kGlyphs; p->name != nullptr; ++p) {
    if (name == p->name) return &p->glyph;
  }
  return nullptr;
}

// Blit a 1-bit, MSB-first packed bitmap of size sz×sz at top-left (x,y).
// Single-color: set pixels stay fg, clear pixels are left untouched so the
// glyph composes onto whatever bg was already there.
void blit_1bit(LovyanGFX& g, int16_t x, int16_t y, int16_t sz,
               const uint8_t* data, uint32_t fg) {
  const int bytes_per_row = (sz + 7) / 8;
  for (int16_t row = 0; row < sz; ++row) {
    const uint8_t* p = data + row * bytes_per_row;
    for (int16_t col = 0; col < sz; ++col) {
      if (p[col >> 3] & (0x80 >> (col & 7))) {
        g.drawPixel(x + col, y + row, fg);
      }
    }
  }
}

}  // namespace

void draw_icon(LovyanGFX& gfx, const IconNode& i, Rect r) {
  int16_t s = i.size > 0 ? i.size : (int16_t)std::min(r.w, r.h);
  uint32_t fg = color_to_m5gfx(i.color);

  const icons::Glyph* glyph = find_glyph(i.name);
  if (glyph == nullptr) {
    // Unknown name → small filled square so a typo in the producer is at
    // least visible during layout review.
    int16_t x = r.x + (r.w - s) / 2;
    int16_t y = r.y + (r.h - s) / 2;
    gfx.fillRect(x, y, s, s, fg);
    return;
  }

  // Pick the closest baked size at or above the requested s. We bake 16 + 24;
  // 16 is the firmware-wide default footer size.
  const uint8_t* px;
  int16_t bake_sz;
  if (s >= 22) {
    px = glyph->px24;
    bake_sz = 24;
  } else {
    px = glyph->px16;
    bake_sz = 16;
  }

  int16_t x = r.x + (r.w - bake_sz) / 2;
  int16_t y = r.y + (r.h - bake_sz) / 2;
  blit_1bit(gfx, x, y, bake_sz, px, fg);
}

enum class ImageFormat {
  Unknown = 0,
  Png,
  Jpg,
  Bmp,
  Qoi,
};

static void draw_image_placeholder(LovyanGFX& gfx, const ImageNode& i, Rect r, const char* label) {
  gfx.drawRect(r.x, r.y, r.w, r.h, 0x000000);
  gfx.setFont(font_for(TextSize::Sm, Weight::Regular));
  gfx.setTextColor(0x000000);
  gfx.setCursor(r.x + 4, r.y + 4);
  gfx.print(label);
  if (!i.name.empty()) {
    gfx.setCursor(r.x + 4, r.y + 18);
    gfx.print(i.name.c_str());
  } else if (!i.url.empty()) {
    gfx.setCursor(r.x + 4, r.y + 18);
    gfx.print(i.url.c_str());
  }
}

static ImageFormat image_format_from_token(const std::string& raw) {
  std::string token;
  token.reserve(raw.size());
  for (char ch : raw) token.push_back((char)std::tolower((unsigned char)ch));
  if (token == "png") return ImageFormat::Png;
  if (token == "jpg" || token == "jpeg") return ImageFormat::Jpg;
  if (token == "bmp") return ImageFormat::Bmp;
  if (token == "qoi") return ImageFormat::Qoi;
  return ImageFormat::Unknown;
}

static ImageFormat image_format_from_content_type(const String& raw) {
  if (raw.startsWith("image/png")) return ImageFormat::Png;
  if (raw.startsWith("image/jpeg") || raw.startsWith("image/jpg")) return ImageFormat::Jpg;
  if (raw.startsWith("image/bmp")) return ImageFormat::Bmp;
  if (raw.startsWith("image/qoi")) return ImageFormat::Qoi;
  return ImageFormat::Unknown;
}

static ImageFormat image_format_from_url(const std::string& raw) {
  String url(raw.c_str());
  int q = url.indexOf('?');
  if (q >= 0) url.remove(q);
  url.toLowerCase();
  if (url.endsWith(".png")) return ImageFormat::Png;
  if (url.endsWith(".jpg") || url.endsWith(".jpeg")) return ImageFormat::Jpg;
  if (url.endsWith(".bmp")) return ImageFormat::Bmp;
  if (url.endsWith(".qoi")) return ImageFormat::Qoi;
  return ImageFormat::Unknown;
}

static bool draw_decoded_image(LovyanGFX& gfx, Stream* stream, ImageFormat fmt, Rect r) {
  switch (fmt) {
    case ImageFormat::Png:
      return gfx.drawPng(stream, r.x, r.y, r.w, r.h, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
    case ImageFormat::Jpg:
      return gfx.drawJpg(stream, r.x, r.y, r.w, r.h, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
    case ImageFormat::Bmp:
      return gfx.drawBmp(stream, r.x, r.y, r.w, r.h, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
    case ImageFormat::Qoi:
      return gfx.drawQoi(stream, r.x, r.y, r.w, r.h, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
    case ImageFormat::Unknown:
    default:
      return false;
  }
}

void draw_image(LovyanGFX& gfx, const ImageNode& i, Rect r) {
  if (!i.url.empty()) {
    HTTPClient http;
    static const char* headers[] = {"Content-Type"};
    http.collectHeaders(headers, 1);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(30000);
    if (!http.begin(String(i.url.c_str()))) {
      draw_image_placeholder(gfx, i, r, "img begin failed");
      return;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      http.end();
      draw_image_placeholder(gfx, i, r, "img http failed");
      return;
    }

    ImageFormat fmt = image_format_from_token(i.format);
    if (fmt == ImageFormat::Unknown) fmt = image_format_from_content_type(http.header("Content-Type"));
    if (fmt == ImageFormat::Unknown) fmt = image_format_from_url(i.url);
    if (fmt == ImageFormat::Unknown) {
      http.end();
      draw_image_placeholder(gfx, i, r, "img fmt?");
      return;
    }

    WiFiClient* stream = http.getStreamPtr();
    bool ok = stream != nullptr && draw_decoded_image(gfx, stream, fmt, r);
    http.end();
    if (!ok) draw_image_placeholder(gfx, i, r, "img decode failed");
    return;
  }

  draw_image_placeholder(gfx, i, r, "img");
}

}  // namespace rcpaper
#endif
