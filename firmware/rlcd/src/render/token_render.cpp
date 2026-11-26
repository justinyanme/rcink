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
#include "render/rlcd_chrome.h"
#include "render/token_layout.h"
#include "render/usage_logos.h"

namespace rcpaper {
namespace {

using rlcd_chrome::Align;
using rlcd_chrome::FontRole;
using rlcd_chrome::kInk;
using rlcd_chrome::kPaper;

constexpr int16_t kChartX = kTokenChartX;
constexpr int16_t kChartRight = kTokenChartRight;
constexpr int16_t kBarPitch = 6;
constexpr int16_t kBarWidth = 4;

TFT_eSprite* g = nullptr;

int16_t text_w(const char* value, FontRole role) {
  return rlcd_chrome::text_w(g, value, role);
}

void text(const char* value, int16_t x, int16_t baseline, FontRole role,
          Align align = Align::Left, uint16_t color = kInk) {
  rlcd_chrome::text(g, value, x, baseline, role, align, color);
}

void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color = kInk) {
  if (w > 0 && h > 0) g->fillRect(x, y, w, h, color);
}

const MonoBitmap* token_logo(const std::string& id) {
  if (id == "claude") return &usage_logos::kClaude28;
  if (id == "codex") return &usage_logos::kCodex28;
  if (id == "gemini") return &usage_logos::kGemini28;
  if (id == "kimi") return &usage_logos::kKimi28;
  if (id == "zai") return &usage_logos::kZai28;
  return nullptr;
}

void headline(const TokenView& view) {
  const bool has_pick = view.headline_pick >= 0;
  if (has_pick) rlcd_chrome::pixels(g, rlcd_chrome::kSpark, kTokenMarginX, 8);
  const int16_t left_x = has_pick ? 27 : kTokenMarginX;
  const FontRole left_role = has_pick ? FontRole::BodyBold : FontRole::Body;
  text(view.headline.c_str(), left_x, kTokenHeaderBaseline, left_role);
  const int16_t left_end = left_x + text_w(view.headline.c_str(), left_role);

  if (!view.total_seven_day.empty()) {
    const int16_t right_w =
        8 + 4 + text_w(view.total_seven_day.c_str(), FontRole::Body);
    const int16_t right_x0 = kChartRight - right_w;
    if (left_end + 10 <= right_x0) {
      rlcd_chrome::pixels(g, rlcd_chrome::kSigma, right_x0, 8);
      text(view.total_seven_day.c_str(), kChartRight, kTokenHeaderBaseline,
           FontRole::Body, Align::Right);
    }
  }
  rect(kTokenMarginX, kTokenHeaderRuleY, 376, 1);
}

void chart(const TokenRowView& row, int16_t bottom, int16_t chart_h,
           float sweep) {
  // Subtle single-pixel tick aligned with each bar center keeps the 30-day span
  // anchored without colliding into dense double-dotted rails on empty/low rows.
  for (size_t i = 0; i < kTokenDays; i++) {
    g->drawPixel(kChartX + (int16_t)i * kBarPitch + 1, bottom, kInk);
  }
  if (row.usual_px > 0 && row.first_active_day < kTokenDays) {
    const int16_t usual_h = std::max<int16_t>(
        4, (int16_t)std::lround((double)row.usual_px * chart_h / kTokenChartPx));
    const int16_t y = bottom - usual_h;
    const int16_t x0 = kChartX + (int16_t)row.first_active_day * kBarPitch;
    for (int16_t x = x0; x < kChartRight; x += 5) rect(x, y, 2, 1);
  }
  const size_t visible =
      std::min(kTokenDays, (size_t)std::lround(sweep * kTokenDays));
  for (size_t i = 0; i < visible; i++) {
    int16_t height = row.bars[i] > 0
        ? std::max<int16_t>(
              1, (int16_t)std::lround((double)row.bars[i] * chart_h / kTokenChartPx))
        : 0;
    const bool today = i == kTokenDays - 1;
    if (today) height = std::max<int16_t>(3, height);  // preserve hollow interior
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

void row(size_t i, size_t count, const TokenRowView& row,
         const TokenFrame& frame) {
  const TokenRowMetrics m = token_row_metrics(count);
  const int16_t top = token_row_top(i, count);
  if (i > 0) {
    for (int16_t x = kTokenMarginX; x < kChartRight; x += 4) {
      g->drawPixel(x, top, kInk);
    }
  }
  if (const MonoBitmap* mark = token_logo(row.id)) {
    g->drawBitmap(kTokenMarginX, top + m.logo_y, mark->bits, mark->w, mark->h,
                  kInk);
  }
  const int16_t hdr_base = top + m.hdr_base;
  const int16_t total_base = top + m.total_base;
  const int16_t today_base = top + m.today_base;
  const std::string name = token_provider_name(row.id);
  const FontRole name_role =
      m.compact_name ? FontRole::BodyBold : FontRole::Name;
  text(name.c_str(), kTokenTextX, hdr_base, name_role);
  int16_t after_name = kTokenTextX + text_w(name.c_str(), name_role);
  if (row.id == "gemini") {
    g->drawBitmap(after_name + 6, hdr_base - 12,
                  usage_logos::kAntigravity12.bits,
                  usage_logos::kAntigravity12.w, usage_logos::kAntigravity12.h,
                  kInk);
    after_name += 6 + usage_logos::kAntigravity12.w;
  }
  text("7 days", after_name + kTokenNameLabelGap, hdr_base, FontRole::Body);
  const FontRole total_role =
      m.total_pt >= 24 ? FontRole::HeroLg
                       : (m.total_pt >= 18 ? FontRole::HeroMd : FontRole::Name);
  text(row.seven_day.c_str(), kTokenTextX, total_base, total_role);
  chart(row, total_base, m.chart_h, frame.sweep);
  rlcd_chrome::draw_dotted_text(g, row.today, kTokenTextX, today_base,
                                FontRole::Body);
  std::string odo = row.lifetime;
  if (frame.sweep < 1.0f) {
    const long double rolled = (long double)row.lifetime_value * frame.sweep;
    const uint64_t value = rolled >= std::numeric_limits<uint64_t>::max()
        ? std::numeric_limits<uint64_t>::max()
        : (uint64_t)rolled;
    odo = format_tokens(value);
  }
  const int16_t value_w = text_w(odo.c_str(), FontRole::BodyBold);
  text("ODO", kChartRight - value_w - kTokenOdoGap, hdr_base, FontRole::Body,
       Align::Right);
  text(odo.c_str(), kChartRight, hdr_base, FontRole::BodyBold, Align::Right);
}

}  // namespace

void render_token(const TokenView& view, const BatteryReading& battery,
                  RuntimeMode mode, const TokenFrame& frame,
                  bool carousel_pinned) {
  g = display_canvas();
  if (!g) return;
  g->fillSprite(kPaper);
  headline(view);
  for (size_t i = 0; i < view.count; i++) row(i, view.count, view.rows[i], frame);
  rlcd_chrome::draw_footer(g, view.footer_chip, view.footer_text, battery, mode,
                           carousel_pinned);
  display_push();
  g = nullptr;
}

}  // namespace rcpaper
#endif
