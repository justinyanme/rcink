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
#include "render/usage_logos.h"

namespace rcpaper {
namespace {
constexpr uint16_t kInk = 0x0000;
constexpr uint16_t kPaper = 0xFFFF;
constexpr int16_t kChartX = 202;
constexpr int16_t kChartRight = 388;
constexpr int16_t kBarPitch = 6;
constexpr int16_t kBarWidth = 4;

TFT_eSprite* g = nullptr;
enum class Align : uint8_t { Left, Right };

const GFXfont* font(int pt, bool bold) {
  if (pt >= 24) return &FreeSansBold24pt7b;
  if (pt >= 12) return bold ? &FreeSansBold12pt7b : &FreeSans12pt7b;
  return bold ? &FreeSansBold9pt7b : &FreeSans9pt7b;
}

int16_t text_w(const char* value, int pt, bool bold = false) {
  g->setFreeFont(font(pt, bold));
  g->setTextSize(1);
  return g->textWidth(value);
}

void text(const char* value, int16_t x, int16_t baseline, int pt,
          bool bold = false, Align align = Align::Left, uint16_t color = kInk) {
  g->setFreeFont(font(pt, bold));
  g->setTextSize(1);
  g->setTextWrap(false, false);
  g->setTextDatum(align == Align::Left ? L_BASELINE : R_BASELINE);
  g->setTextColor(color);
  g->drawString(value, x, baseline);
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
  const int16_t width = text_w(value, 9, true) + 10;
  rect(x, baseline - 15, width, 19);
  text(value, x + 5, baseline, 9, true, Align::Left, kPaper);
  return width;
}

const char* const kSpark[] = {
    "....#....", "....#....", "....#....", "...###...", "#########",
    "...###...", "....#....", "....#....", "....#....",
};
const MonoBitmap* const kLogos[kTokenProviderCount] = {
    &usage_logos::kClaude28, &usage_logos::kCodex28, &usage_logos::kGemini28};

void headline(const TokenView& view) {
  if (view.headline_pick >= 0) pixels(kSpark, 12, 6);
  text(view.headline.c_str(), view.headline_pick >= 0 ? 28 : 12, 18, 9,
       view.headline_pick >= 0);
  rect(12, 24, 376, 1);
}

void chart(const TokenRowView& row, int16_t top, float sweep) {
  const int16_t bottom = top + 47;
  for (int16_t x = kChartX; x < kChartRight; x += 4) g->drawPixel(x, bottom, kInk);
  if (row.usual_px > 0) {
    const int16_t y = bottom - row.usual_px;
    for (int16_t x = kChartX; x < kChartRight; x += 5) rect(x, y, 2, 1);
  }
  const size_t visible = std::min(kTokenDays, (size_t)std::lround(sweep * kTokenDays));
  for (size_t i = 0; i < visible; i++) {
    int16_t height = row.bars[i];
    const bool today = i == kTokenDays - 1;
    if (today && height == 0) height = 3;  // an empty, incomplete day stays visible
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

void row(size_t i, const TokenRowView& row, const TokenFrame& frame) {
  const int16_t top = 26 + (int16_t)i * 82;
  if (i > 0) rect(12, top - 1, 376, 1);
  g->drawBitmap(12, top + 18, kLogos[i]->bits, kLogos[i]->w, kLogos[i]->h, kInk);
  text(kTokenProviderNames[i], 48, top + 18, 12, true);
  if (i == 2) {
    g->drawBitmap(48 + text_w(kTokenProviderNames[i], 12, true) + 6, top + 6,
                  usage_logos::kAntigravity12.bits, usage_logos::kAntigravity12.w,
                  usage_logos::kAntigravity12.h, kInk);
  }
  text(row.seven_day.c_str(), 48, top + 50, 24, true);
  const int16_t label_x = 48 + text_w(row.seven_day.c_str(), 24, true) + 7;
  text("7 days", label_x, top + 48, 9);
  chart(row, top, frame.sweep);
  text(row.today.c_str(), 48, top + 70, 9);
  std::string odo = row.lifetime;
  if (frame.sweep < 1.0f) {
    const long double rolled = (long double)row.lifetime_value * frame.sweep;
    const uint64_t value = rolled >= std::numeric_limits<uint64_t>::max()
        ? std::numeric_limits<uint64_t>::max() : (uint64_t)rolled;
    odo = format_tokens(value);
  }
  const int16_t value_w = text_w(odo.c_str(), 12, true);
  text("ODO", 388 - value_w - 7, top + 70, 9, false, Align::Right);
  text(odo.c_str(), 388, top + 70, 12, true, Align::Right);
}

void footer(const TokenView& view, const BatteryReading& battery, RuntimeMode mode) {
  rect(12, 275, 376, 1);
  constexpr int16_t baseline = 294;
  int16_t right = 388;
  const int16_t mode_w = text_w(runtime_mode_name(mode), 9, true) + 10;
  chip(runtime_mode_name(mode), right - mode_w, baseline);
  right -= mode_w + 8;
  char percent[8];
  std::snprintf(percent, sizeof(percent), battery.ok ? "%u%%" : "--%%",
                (unsigned)battery.pct);
  text(percent, right, baseline, 9, false, Align::Right);
  right -= text_w(percent, 9) + 6;
  const int16_t x = right - 24;
  g->drawRect(x, 283, 22, 12, kInk);
  rect(x + 22, 287, 2, 4);
  if (battery.ok) rect(x + 2, 285, (18 * std::min<unsigned>(battery.pct, 100) + 50) / 100, 8);
  if (!view.footer_chip.empty()) {
    const int16_t width = chip(view.footer_chip.c_str(), 12, baseline);
    text(view.footer_text.c_str(), 12 + width + 6, baseline, 9);
  } else {
    text(view.footer_text.c_str(), 12, baseline, 9);
  }
}
}  // namespace

void render_token(const TokenView& view, const BatteryReading& battery,
                  RuntimeMode mode, const TokenFrame& frame) {
  g = display_canvas();
  if (!g) return;
  g->fillSprite(kPaper);
  headline(view);
  for (size_t i = 0; i < view.rows.size(); i++) row(i, view.rows[i], frame);
  footer(view, battery, mode);
  display_push();
  g = nullptr;
}

}  // namespace rcpaper
#endif
