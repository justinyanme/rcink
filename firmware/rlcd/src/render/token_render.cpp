// Hand-drawn Token page for the 400x300 1 bpp RLCD. The pure TokenView is the
// layout oracle; this file only paints it. HOST sweep reveals bars left-to-right
// and rolls each lifetime odometer up; all other frames are static.
#ifdef ARDUINO

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "render/canvas.h"
#include "render/page.h"
#include "render/rlcd_fonts.h"
#include "render/token_layout.h"
#include "render/usage_logos.h"

namespace rcpaper {
namespace {
constexpr uint16_t kInk = 0x0000;
constexpr uint16_t kPaper = 0xFFFF;
constexpr int16_t kChartX = kTokenChartX;
constexpr int16_t kChartRight = kTokenChartRight;
constexpr int16_t kBarPitch = 6;
constexpr int16_t kBarWidth = 4;

TFT_eSprite* g = nullptr;
enum class Align : uint8_t { Left, Right };
enum class FontRole : uint8_t { Chip, Body, BodyBold, Name, HeroMd, HeroLg };

const GFXfont* font_for(FontRole role) {
  switch (role) {
    case FontRole::Chip:     return &rlcd_fonts::kBarlowChip15;
    case FontRole::Body:     return &rlcd_fonts::kBarlowBody18;
    case FontRole::BodyBold: return &rlcd_fonts::kBarlowBodyBold18;
    case FontRole::Name:     return &rlcd_fonts::kBarlowName24;
    case FontRole::HeroMd:   return &rlcd_fonts::kBarlowHero36;
    case FontRole::HeroLg:   return &rlcd_fonts::kBarlowHero48;
  }
  return &rlcd_fonts::kBarlowBody18;
}

int16_t text_w(const char* value, FontRole role) {
  g->setFreeFont(font_for(role));
  g->setTextSize(1);
  return g->textWidth(value);
}

void text(const char* value, int16_t x, int16_t baseline, FontRole role,
          Align align = Align::Left, uint16_t color = kInk) {
  g->setFreeFont(font_for(role));
  g->setTextSize(1);
  g->setTextWrap(false, false);
  g->setTextDatum(align == Align::Left ? L_BASELINE : R_BASELINE);
  g->setTextColor(color);
  g->drawString(value, x, baseline);
}

// Preserve the wording and draw a symmetric dot separator.
void text_with_dots(const std::string& value, int16_t x, int16_t baseline) {
  const std::string separator = "\xC2\xB7";
  const size_t at = value.find(separator);
  if (at == std::string::npos) {
    text(value.c_str(), x, baseline, FontRole::Body);
    return;
  }
  std::string left = value.substr(0, at);
  std::string right = value.substr(at + separator.size());
  while (!left.empty() && left.back() == ' ') left.pop_back();
  size_t r0 = 0;
  while (r0 < right.size() && right[r0] == ' ') r0++;
  right = right.substr(r0);
  text(left.c_str(), x, baseline, FontRole::Body);
  const int16_t dot_x = x + text_w(left.c_str(), FontRole::Body) + kTokenDotGap;
  g->fillRect(dot_x, baseline - 5, 2, 2, kInk);
  text(right.c_str(), dot_x + 2 + kTokenDotGap, baseline, FontRole::Body);
}

void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color = kInk) {
  if (w > 0 && h > 0) g->fillRect(x, y, w, h, color);
}

template <size_t N>
void pixels(const char* const (&rows)[N], int16_t x, int16_t y) {
  for (size_t j = 0; j < N; j++) {
    for (size_t i = 0; rows[j][i]; i++) {
      if (rows[j][i] == '#') g->drawPixel(x + (int16_t)i, y + (int16_t)j, kInk);
    }
  }
}

int16_t chip(const char* value, int16_t x, int16_t baseline) {
  const int16_t width = text_w(value, FontRole::Chip) + kTokenChipExtraW;
  rect(x, baseline - 13, width, 17);
  text(value, x + kTokenChipPadding, baseline, FontRole::Chip, Align::Left, kPaper);
  return width;
}

const char* const kSpark[] = {
    "....#....", "....#....", "....#....", "...###...", "#########",
    "...###...", "....#....", "....#....", "....#....",
};
const MonoBitmap* token_logo(const std::string& id) {
  if (id == "claude") return &usage_logos::kClaude28;
  if (id == "codex") return &usage_logos::kCodex28;
  if (id == "gemini") return &usage_logos::kGemini28;
  return nullptr;
}

void headline(const TokenView& view) {
  if (view.headline_pick >= 0) pixels(kSpark, kTokenMarginX, 9);
  text(view.headline.c_str(), view.headline_pick >= 0 ? 26 : kTokenMarginX,
       kTokenHeaderBaseline,
       view.headline_pick >= 0 ? FontRole::BodyBold : FontRole::Body);
  text("since Sep '25", kTokenChartRight, kTokenHeaderBaseline, FontRole::Body, Align::Right);
  rect(kTokenMarginX, kTokenHeaderRuleY, 376, 1);
}

void chart(const TokenRowView& row, int16_t bottom, int16_t chart_h, float sweep) {
  for (int16_t x = kChartX; x < kChartRight; x += 4) g->drawPixel(x, bottom, kInk);
  if (row.usual_px > 0) {
    const int16_t usual_h = std::max<int16_t>(
        1, (int16_t)std::lround((double)row.usual_px * chart_h / kTokenChartPx));
    const int16_t y = bottom - usual_h;
    for (int16_t x = kChartX; x < kChartRight; x += 5) rect(x, y, 2, 1);
  }
  const size_t visible = std::min(kTokenDays, (size_t)std::lround(sweep * kTokenDays));
  for (size_t i = 0; i < visible; i++) {
    int16_t height = row.bars[i] > 0
        ? std::max<int16_t>(1, (int16_t)std::lround((double)row.bars[i] * chart_h / kTokenChartPx))
        : 0;
    const bool today = i == kTokenDays - 1;
    if (today) height = std::max<int16_t>(3, height);  // preserve a hollow interior
    if (height == 0) continue;
    const int16_t x = kChartX + (int16_t)i * kBarPitch;
    if (today) {
      rect(x, bottom - height, kBarWidth, height, kPaper);
      g->drawRect(x, bottom - height, kBarWidth, height, kInk);
    } else {
      rect(x, bottom - height, kBarWidth, height);
    }
  }
}

void row(size_t i, size_t count, const TokenRowView& row, const TokenFrame& frame) {
  const TokenRowMetrics m = token_row_metrics(count);
  const int16_t top = token_row_top(i, count);
  if (i > 0) for (int16_t x = kTokenMarginX; x < kChartRight; x += 4) g->drawPixel(x, top, kInk);
  if (const MonoBitmap* mark = token_logo(row.id)) {
    g->drawBitmap(kTokenMarginX, top + m.logo_y, mark->bits, mark->w, mark->h, kInk);
  }
  const int16_t hdr_base = top + m.hdr_base;
  const int16_t total_base = top + m.total_base;
  const int16_t today_base = top + m.today_base;
  const std::string name = token_provider_name(row.id);
  text(name.c_str(), kTokenTextX, hdr_base, FontRole::Name);
  int16_t after_name = kTokenTextX + text_w(name.c_str(), FontRole::Name);
  if (row.id == "gemini") {
    g->drawBitmap(after_name + 6, hdr_base - 12,
                  usage_logos::kAntigravity12.bits, usage_logos::kAntigravity12.w,
                  usage_logos::kAntigravity12.h, kInk);
    after_name += 6 + usage_logos::kAntigravity12.w;
  }
  text("7 days", after_name + kTokenNameLabelGap, hdr_base, FontRole::Body);
  text(row.seven_day.c_str(), kTokenTextX, total_base,
       m.total_pt >= 24 ? FontRole::HeroLg : FontRole::HeroMd);
  chart(row, total_base, m.chart_h, frame.sweep);
  text_with_dots(row.today, kTokenTextX, today_base);
  std::string odo = row.lifetime;
  if (frame.sweep < 1.0f) {
    const long double rolled = (long double)row.lifetime_value * frame.sweep;
    const uint64_t value = rolled >= std::numeric_limits<uint64_t>::max()
        ? std::numeric_limits<uint64_t>::max() : (uint64_t)rolled;
    odo = format_tokens(value);
  }
  const int16_t value_w = text_w(odo.c_str(), FontRole::BodyBold);
  text("ODO", kTokenChartRight - value_w - kTokenOdoGap, hdr_base,
       FontRole::Body, Align::Right);
  text(odo.c_str(), kTokenChartRight, hdr_base,
       FontRole::BodyBold, Align::Right);
}

void footer(const TokenView& view, const BatteryReading& battery, RuntimeMode mode) {
  rect(kTokenMarginX, kTokenFooterRuleY, 376, 1);
  constexpr int16_t baseline = kTokenFooterBaseline;
  int16_t right = kTokenChartRight;
  const int16_t mode_w = text_w(runtime_mode_name(mode), FontRole::Chip) +
                         kTokenChipExtraW;
  chip(runtime_mode_name(mode), right - mode_w, baseline);
  right -= mode_w + kTokenModeGap;
  char percent[8];
  std::snprintf(percent, sizeof(percent), battery.ok ? "%u%%" : "--%%",
                (unsigned)battery.pct);
  text(percent, right, baseline, FontRole::Body, Align::Right);
  right -= text_w(percent, FontRole::Body) + kTokenBatteryGap;
  const int16_t x = right - 24;
  g->drawRect(x, kTokenBatteryY, 22, 12, kInk);
  rect(x + 22, kTokenBatteryY + 4, 2, 4);
  if (battery.ok) {
    rect(x + 2, kTokenBatteryY + 2,
         (18 * std::min<unsigned>(battery.pct, 100) + 50) / 100, 8);
  }
  if (!view.footer_chip.empty()) {
    const int16_t width = chip(view.footer_chip.c_str(), kTokenMarginX, baseline);
    text(view.footer_text.c_str(), kTokenMarginX + width + kTokenFooterTextGap, baseline,
         FontRole::Body);
  } else {
    text(view.footer_text.c_str(), kTokenMarginX, baseline, FontRole::Body);
  }
}
}  // namespace

void render_token(const TokenView& view, const BatteryReading& battery,
                  RuntimeMode mode, const TokenFrame& frame) {
  g = display_canvas();
  if (!g) return;
  g->fillSprite(kPaper);
  headline(view);
  for (size_t i = 0; i < view.count; i++) row(i, view.count, view.rows[i], frame);
  footer(view, battery, mode);
  display_push();
  g = nullptr;
}

}  // namespace rcpaper
#endif
