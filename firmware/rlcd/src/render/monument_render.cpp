// Hand-drawn Page 3 ("year-monument"): Desk Clock, Total Odometer, 53x7 Annual
// Token Heatmap, Milestone Progress, and Indoor Micro-Climate on the 400x300
// 1-bit RLCD.
#ifdef ARDUINO

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>

#include "render/canvas.h"
#include "render/monument_layout.h"
#include "render/monument_view.h"
#include "render/page.h"
#include "render/rlcd_chrome.h"
#include "render/token_view.h"

namespace rcpaper {
namespace {

using rlcd_chrome::Align;
using rlcd_chrome::FontRole;
using rlcd_chrome::kInk;
using rlcd_chrome::kPaper;

TFT_eSprite* g = nullptr;

// Compact 3x5 day-of-week letters so row labels sit cleanly in x=12..15.
const char* const kGlyphM[] = {"#.#", "###", "#.#", "#.#", "#.#"};
const char* const kGlyphW[] = {"#.#", "#.#", "###", "###", "#.#"};
const char* const kGlyphF[] = {"###", "#..", "##.", "#..", "#.."};

void draw_cell(int16_t x, int16_t y, int16_t w, uint8_t level) {
  constexpr int16_t h = kMonumentCellSize;
  switch (level) {
    case 0:
      g->drawPixel(x + w / 2, y + 2, kInk);
      break;
    case 1:
      g->drawRect(x, y, w, h, kInk);
      break;
    case 2:
      g->drawRect(x, y, w, h, kInk);
      for (int16_t dx = 2; dx <= w - 3; dx += 2) {
        g->drawPixel(x + dx, y + 2, kInk);
      }
      break;
    case 3:
      g->fillRect(x, y, w, h, kInk);
      for (int16_t dx = 2; dx <= w - 3; dx += 2) {
        g->drawPixel(x + dx, y + 2, kPaper);
      }
      break;
    case 4:
    default:
      g->fillRect(x, y, w, h, kInk);
      break;
  }
}

void draw_masthead(const MonumentView& view, const Shtc3Reading& env) {
  rlcd_chrome::draw_dotted_text(g, view.header_left, kMonumentMarginX,
                                kMonumentHeaderBaseline, FontRole::BodyBold);
  const int16_t left_end =
      kMonumentMarginX +
      rlcd_chrome::dotted_text_width(g, view.header_left, FontRole::BodyBold);
  rlcd_chrome::draw_ambient_climate_right(g, env, kMonumentRightX,
                                          kMonumentHeaderBaseline,
                                          left_end + 10, true);
  g->fillRect(kMonumentMarginX, kMonumentHeaderRuleY, 376, 1, kInk);
}

void draw_hero(const MonumentView& view, const MonumentFrame& frame) {
  rlcd_chrome::text(g, view.clock_caption.c_str(), kMonumentMarginX,
                    kMonumentHeroCaptionBaseline, FontRole::Body);
  const size_t colon_pos = view.clock_hhmm.find(':');
  if (colon_pos != std::string::npos) {
    // Barlow's default ':' sits on the lowercase baseline (11 px lower than the
    // top of 34 px digits). Position digits by exact xAdvance and raise ':' by
    // 6 px to match OpenType case-sensitive clock centering cleanly.
    const GFXfont* font = rlcd_chrome::font_for(FontRole::HeroLg);
    const std::string hh = view.clock_hhmm.substr(0, colon_pos);
    const std::string mm = view.clock_hhmm.substr(colon_pos + 1);
    int16_t colon_x = kMonumentMarginX;
    for (unsigned char ch : hh) {
      if (ch >= font->first && ch <= font->last) {
        colon_x += font->glyph[ch - font->first].xAdvance;
      }
    }
    int16_t mm_x = colon_x;
    if (':' >= font->first && ':' <= font->last) {
      mm_x += font->glyph[':' - font->first].xAdvance;
    }
    rlcd_chrome::text(g, hh.c_str(), kMonumentMarginX, kMonumentHeroBaseline,
                      FontRole::HeroLg);
    rlcd_chrome::text(g, mm.c_str(), mm_x, kMonumentHeroBaseline,
                      FontRole::HeroLg);
    if (frame.show_colon || view.clock_hhmm == "--:--") {
      rlcd_chrome::text(g, ":", colon_x, kMonumentHeroBaseline - 6,
                        FontRole::HeroLg);
    }
  } else {
    rlcd_chrome::text(g, view.clock_hhmm.c_str(), kMonumentMarginX,
                      kMonumentHeroBaseline, FontRole::HeroLg);
  }

  for (int16_t y = 35; y < 90; y += 4) {
    g->drawPixel(kMonumentHeroSepX, y, kInk);
  }

  rlcd_chrome::text(g, "TOTAL ODO", kMonumentOdoX, kMonumentHeroCaptionBaseline,
                    FontRole::Body);
  const int16_t days_w =
      rlcd_chrome::text_w(g, view.active_badge.c_str(), FontRole::Body);
  rlcd_chrome::text(g, view.active_badge.c_str(), kMonumentRightX,
                    kMonumentHeroCaptionBaseline, FontRole::Body, Align::Right);
  if (!view.window_label.empty()) {
    rlcd_chrome::chip(g, view.window_label.c_str(),
                      (int16_t)(kMonumentRightX - days_w - kMonumentWindowBadgeGap),
                      kMonumentHeroCaptionBaseline, Align::Right);
  }

  std::string odo = view.total_odo;
  if (frame.sweep < 1.0f && view.total_odo_value > 0) {
    const long double rolled = (long double)view.total_odo_value * frame.sweep;
    const uint64_t val = rolled >= std::numeric_limits<uint64_t>::max()
        ? std::numeric_limits<uint64_t>::max()
        : (uint64_t)rolled;
    odo = format_tokens(val);
  }
  rlcd_chrome::text(g, odo.c_str(), kMonumentOdoX, kMonumentHeroBaseline,
                    FontRole::HeroLg);

  rlcd_chrome::draw_dotted_text(g, view.summary_left, kMonumentMarginX,
                                kMonumentSummaryBaseline, FontRole::Body);
  if (!view.summary_right.empty()) {
    const int16_t text_w =
        rlcd_chrome::dotted_text_width(g, view.summary_right, FontRole::Body);
    const int16_t x0 =
        (int16_t)(kMonumentRightX -
                  (kMonumentSigmaGlyphW + kMonumentSigmaGap + text_w));
    rlcd_chrome::pixels(g, rlcd_chrome::kSigma, x0,
                        kMonumentSummaryBaseline - 11);
    rlcd_chrome::draw_dotted_text(
        g, view.summary_right,
        (int16_t)(x0 + kMonumentSigmaGlyphW + kMonumentSigmaGap),
        kMonumentSummaryBaseline, FontRole::Body);
  }
  for (int16_t x = kMonumentMarginX; x < kMonumentRightX; x += 4) {
    g->drawPixel(x, kMonumentHeroDividerY, kInk);
  }
}

void draw_heatmap(const MonumentView& view, const MonumentFrame& frame) {
  const size_t total_cols = std::min<size_t>(
      view.cols == 0 ? kMonumentMaxCols : view.cols, kMonumentMaxCols);
  int16_t last_month_right = -100;
  for (size_t i = 0; i < view.month_count; i++) {
    const MonumentGridCol mc = monument_grid_col(view.months[i].col, total_cols);
    const int16_t w =
        rlcd_chrome::text_w(g, view.months[i].label, FontRole::Chip);
    const int16_t x = std::min<int16_t>((int16_t)(kMonumentRightX - w), mc.x);
    if (x >= last_month_right + 8) {
      rlcd_chrome::text(g, view.months[i].label, x, kMonumentMonthBaseline,
                        FontRole::Chip);
      last_month_right = x + w;
    }
  }

  rlcd_chrome::pixels(g, kGlyphM, kMonumentMarginX, kMonumentGridY + 0);
  rlcd_chrome::pixels(g, kGlyphW, kMonumentMarginX,
                      kMonumentGridY + 2 * kMonumentCellPitch);
  rlcd_chrome::pixels(g, kGlyphF, kMonumentMarginX,
                      kMonumentGridY + 4 * kMonumentCellPitch);

  const size_t visible_cols = std::min(
      total_cols, (size_t)std::lround(frame.sweep * total_cols));
  for (size_t col = 0; col < visible_cols; col++) {
    const MonumentGridCol gc = monument_grid_col(col, total_cols);
    for (size_t row = 0; row < kMonumentRows; row++) {
      const uint8_t lvl = view.cells[col * kMonumentRows + row];
      if (lvl == kMonumentCellOutside) continue;
      draw_cell(gc.x, kMonumentGridY + (int16_t)row * kMonumentCellPitch,
                gc.w, lvl);
    }
  }
  for (int16_t x = kMonumentMarginX; x < kMonumentRightX; x += 4) {
    g->drawPixel(x, kMonumentGridDividerY, kInk);
  }
}

const char* climate_verdict(const Shtc3Reading& env) {
  if (!env.ok) return "OFFLINE";
  if (env.temp_c > 27.0f) return "WARM";
  if (env.temp_c < 18.0f) return "COOL";
  if (env.rh > 68.0f) return "HUMID";
  if (env.rh < 32.0f) return "DRY";
  return "COMFORTABLE";
}

void draw_lower_strip(const MonumentView& view, const Shtc3Reading& env,
                      const MonumentFrame& frame) {
  const int16_t lbl_w = rlcd_chrome::text_w(g, view.milestone_label.c_str(),
                                            FontRole::BodyBold);
  rlcd_chrome::text(g, view.milestone_label.c_str(), kMonumentMarginX,
                    kMonumentMilestoneBaseline, FontRole::BodyBold);

  const int16_t less_w = rlcd_chrome::text_w(g, "LESS", FontRole::Chip);
  const int16_t more_w = rlcd_chrome::text_w(g, "MORE", FontRole::Chip);
  const int16_t legend_cells_w = 5 * 5 + 4 * 3;
  const int16_t legend_left =
      kMonumentRightX - (less_w + 4 + legend_cells_w + 4 + more_w);
  rlcd_chrome::text(g, "LESS", legend_left, kMonumentMilestoneBaseline,
                    FontRole::Chip);
  for (uint8_t lvl = 0; lvl <= 4; lvl++) {
    draw_cell(legend_left + less_w + 4 + (int16_t)lvl * 8,
              kMonumentMilestoneBaseline - 8, kMonumentCellSize, lvl);
  }
  rlcd_chrome::text(g, "MORE", kMonumentRightX, kMonumentMilestoneBaseline,
                    FontRole::Chip, Align::Right);

  char pct_buf[8];
  std::snprintf(pct_buf, sizeof(pct_buf), "%u%%", (unsigned)view.milestone_pct);
  const int16_t pct_w = rlcd_chrome::text_w(g, pct_buf, FontRole::Body);
  const int16_t pct_right = legend_left - 12;
  rlcd_chrome::text(g, pct_buf, pct_right, kMonumentMilestoneBaseline,
                    FontRole::Body, Align::Right);

  const int16_t bar_x = kMonumentMarginX + lbl_w + 10;
  const int16_t bar_w = (pct_right - pct_w - 8) - bar_x;
  if (bar_w >= 24) {
    g->drawRect(bar_x, kMonumentMilestoneBarY, bar_w, kMonumentMilestoneBarH,
                kInk);
    const int16_t fill = (int16_t)std::lround(
        ((bar_w - 4) * (double)view.milestone_pct / 100.0) * frame.sweep);
    if (fill > 0) {
      g->fillRect(bar_x + 2, kMonumentMilestoneBarY + 2, fill,
                  kMonumentMilestoneBarH - 4, kInk);
    }
  }

  // Row 2: Desk micro-climate reading + comfort zone verdict.
  rlcd_chrome::text(g, "DESK CLIMATE", kMonumentMarginX,
                    kMonumentClimateBaseline, FontRole::BodyBold);
  const int16_t title_w =
      rlcd_chrome::text_w(g, "DESK CLIMATE", FontRole::BodyBold);
  if (env.ok) {
    char t_buf[12];
    char h_buf[16];
    std::snprintf(t_buf, sizeof(t_buf), "%.1f", env.temp_c);
    std::snprintf(h_buf, sizeof(h_buf), "%.0f%% RH", env.rh);
    int16_t cx = kMonumentMarginX + title_w + 12;
    rlcd_chrome::text(g, t_buf, cx, kMonumentClimateBaseline, FontRole::Body);
    cx += rlcd_chrome::text_w(g, t_buf, FontRole::Body) + 1;
    rlcd_chrome::pixels(g, rlcd_chrome::kDegRing4, cx,
                        kMonumentClimateBaseline - 12);
    cx += 5;
    rlcd_chrome::text(g, "C", cx, kMonumentClimateBaseline, FontRole::Body);
    cx += rlcd_chrome::text_w(g, "C", FontRole::Body) + rlcd_chrome::kDotGap;
    g->fillRect(cx, kMonumentClimateBaseline - 5, 2, 2, kInk);
    cx += 2 + rlcd_chrome::kDotGap;
    rlcd_chrome::text(g, h_buf, cx, kMonumentClimateBaseline, FontRole::Body);
  } else {
    rlcd_chrome::text(g, "SHTC3 sensor unavailable",
                      kMonumentMarginX + title_w + 12, kMonumentClimateBaseline,
                      FontRole::Body);
  }
  rlcd_chrome::chip(g, climate_verdict(env), kMonumentRightX,
                    kMonumentClimateBaseline, Align::Right);
}

}  // namespace

void render_monument(const MonumentView& view, const Shtc3Reading& env,
                     const BatteryReading& batt, RuntimeMode runtime_mode,
                     const MonumentFrame& frame, bool carousel_pinned) {
  g = display_canvas();
  if (!g) return;
  g->fillSprite(kPaper);
  draw_masthead(view, env);
  draw_hero(view, frame);
  draw_heatmap(view, frame);
  draw_lower_strip(view, env, frame);
  rlcd_chrome::draw_footer(g, view.footer_chip, view.footer_text, batt,
                           runtime_mode, carousel_pinned);
  display_push();
  g = nullptr;
}

}  // namespace rcpaper
#endif
