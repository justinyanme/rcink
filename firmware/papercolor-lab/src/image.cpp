#include "image.h"

#include <cstring>
#include <initializer_list>

#include "fonts.h"

namespace rclab {
namespace {

using lgfx::v1::GFXfont;
using lgfx::v1::GFXglyph;
namespace fonts = rcpaper::baked_fonts;

constexpr unsigned kBorder = 4;
constexpr unsigned kMargin = 8;
constexpr char kTextSample[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
constexpr char kInverseSample[] = "Inverse 16pt 0123456789 xyz";

// production src/dev/waveform_trial.cpp kNative: ink code + sRGB anchor.
struct Anchor {
  Ink ink;
  int r, g, b;
};
constexpr Anchor kAnchors[] = {
    {InkBlack, 0, 0, 0},     {InkWhite, 255, 255, 255}, {InkYellow, 255, 243, 56},
    {InkRed, 191, 0, 0},     {InkBlue, 100, 64, 255},   {InkGreen, 67, 138, 28},
};

Ink nearest(int r, int g, int b) {
  uint32_t best = UINT32_MAX;
  Ink ink = InkBlack;
  for (const auto& a : kAnchors) {
    const int dr = r - a.r, dg = g - a.g, db = b - a.b;
    const uint32_t distance = uint32_t(dr * dr + dg * dg + db * db);
    if (distance < best) best = distance, ink = a.ink;  // first wins ties, as the 10-13 nearest()
  }
  return ink;
}

Ink chart_pixel(unsigned x, unsigned y) {
  if (x < kBorder || x >= kPanelWidth - kBorder || y < kBorder || y >= kPanelHeight - kBorder) return InkBlack;
  if (y < 300) return kAnchors[(y / 100) * 2 + x / 200].ink;
  if (y < 500) {
    const Anchor& a = kAnchors[2 + (y - 300) / 50];
    const int value = int(x * 255 / (kPanelWidth - 1));
    // Same integer truncation as the 10-13 chart (uint8_t of a*value/255).
    return nearest(a.r * value / 255, a.g * value / 255, a.b * value / 255);
  }
  return kAnchors[((x / 20) + (y / 20)) & 1].ink;
}

const GFXfont& font(FontSize size) {
  switch (size) {
    case FontSize::Pt13: return fonts::VT323_13pt7b;
    case FontSize::Pt16: return fonts::VT323_16pt7b;
    case FontSize::Pt18: return fonts::VT323_18pt7b;
    case FontSize::Pt24: return fonts::VT323_24pt7b;
  }
  return fonts::VT323_16pt7b;
}

void plot(uint8_t* inks, int x, int y, uint8_t ink) {
  if (x < 0 || y < 0 || x >= int(kPanelWidth) || y >= int(kPanelHeight)) return;
  inks[size_t(y) * kPanelWidth + size_t(x)] = ink;
}

// Adafruit GFX drawChar: bits MSB first, rows packed back to back. Each font pixel becomes a
// `scale` x `scale` block.
void draw_glyph(uint8_t* inks, const GFXfont& f, const GFXglyph& g, int x, int y, uint8_t ink, int scale = 1) {
  const uint8_t* bits = f.bitmap + g.bitmapOffset;
  unsigned bit = 0;
  for (int row = 0; row < g.height; ++row) {
    for (int col = 0; col < g.width; ++col, ++bit) {
      if (!(bits[bit / 8] & (0x80 >> (bit % 8)))) continue;
      const int px = x + (g.xOffset + col) * scale, py = y + (g.yOffset + row) * scale;
      for (int dy = 0; dy < scale; ++dy)
        for (int dx = 0; dx < scale; ++dx) plot(inks, px + dx, py + dy, ink);
    }
  }
}

// Draws as many glyphs of `text` as fit before kPanelWidth - kMargin; returns the count.
size_t draw_fitting(uint8_t* inks, FontSize size, unsigned y, const char* text, Ink ink) {
  const GFXfont& f = font(size);
  unsigned x = kMargin;
  size_t n = 0;
  for (; text[n]; ++n) {
    const GFXglyph& g = f.glyph[uint8_t(text[n]) - f.first];
    if (x + g.xAdvance > kPanelWidth - kMargin) break;
    draw_glyph(inks, f, g, int(x), int(y), ink);
    x += g.xAdvance;
  }
  return n;
}

}  // namespace

void render_fill(uint8_t* inks, Ink ink) { memset(inks, ink, kPixels); }

void render_chart(uint8_t* inks) {
  for (unsigned y = 0; y < kPanelHeight; ++y) {
    for (unsigned x = 0; x < kPanelWidth; ++x) inks[size_t(y) * kPanelWidth + x] = chart_pixel(x, y);
  }
}

void render_checker(uint8_t* inks, unsigned size) {
  for (unsigned y = 0; y < kPanelHeight; ++y) {
    for (unsigned x = 0; x < kPanelWidth; ++x) {
      inks[size_t(y) * kPanelWidth + x] = ((x / size + y / size) & 1) ? InkWhite : InkBlack;
    }
  }
}

void render_text(uint8_t* inks) {
  render_fill(inks, InkWhite);
  unsigned top = kMargin;
  for (FontSize size : {FontSize::Pt13, FontSize::Pt16, FontSize::Pt18, FontSize::Pt24}) {
    const int ascent = -font(size).glyph['A' - font(size).first].yOffset;
    for (const char* rest = kTextSample; *rest;) {
      rest += draw_fitting(inks, size, top + unsigned(ascent), rest, InkBlack);
      top += line_height(size);
    }
    top += kMargin;
  }
  const unsigned band = line_height(FontSize::Pt16) + kMargin;
  memset(inks + size_t(top) * kPanelWidth, InkBlack, size_t(band) * kPanelWidth);
  const int ascent = -font(FontSize::Pt16).glyph['A' - font(FontSize::Pt16).first].yOffset;
  draw_fitting(inks, FontSize::Pt16, top + kMargin / 2 + unsigned(ascent), kInverseSample, InkWhite);
}

void render_bands(uint8_t* inks) {
  constexpr unsigned kBand = kPanelHeight / 8;
  for (unsigned y = 0; y < kPanelHeight; ++y) memset(inks + size_t(y) * kPanelWidth, int(y / kBand), kPanelWidth);
}

void render_grid(uint8_t* inks) {
  for (unsigned y = 0; y < kPanelHeight; ++y) {
    for (unsigned x = 0; x < kPanelWidth; ++x) inks[size_t(y) * kPanelWidth + x] = uint8_t((y / 75) * 8 + x / 50);
  }
}

void invert(uint8_t* inks, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (inks[i] == InkBlack) inks[i] = InkWhite;
    else if (inks[i] == InkWhite) inks[i] = InkBlack;
  }
}

unsigned draw_text(uint8_t* inks, FontSize size, unsigned x, unsigned y, const char* text, uint8_t ink,
                   unsigned scale) {
  const GFXfont& f = font(size);
  for (const char* p = text; *p; ++p) {
    const GFXglyph& g = f.glyph[uint8_t(*p) - f.first];
    draw_glyph(inks, f, g, int(x), int(y), ink, int(scale));
    x += g.xAdvance * scale;
  }
  return x;
}

unsigned line_height(FontSize size) { return font(size).yAdvance; }

}  // namespace rclab
