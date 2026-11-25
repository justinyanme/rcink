// Hand-drawn Page 1 ("desk-station"): Monument Spine home overview combining
// Desk Clock / ECO Last Sync, Total Odometer, 7-row Heatmap Window, Ambient
// Desk Climate, and 1..4 Provider Allowance + 7D Token strips on 400x300 1bpp.
#ifdef ARDUINO

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "render/canvas.h"
#include "render/page.h"
#include "render/rlcd_chrome.h"
#include "render/station_layout.h"
#include "render/station_view.h"
#include "render/usage_logos.h"

namespace rcpaper {
namespace {

using rlcd_chrome::Align;
using rlcd_chrome::FontRole;
using rlcd_chrome::kInk;
using rlcd_chrome::kPaper;

TFT_eSprite* g = nullptr;

const char* const kGlyphM[] = {"#.#", "###", "#.#", "#.#", "#.#"};
const char* const kGlyphW[] = {"#.#", "#.#", "###", "###", "#.#"};
const char* const kGlyphF[] = {"###", "#..", "##.", "#..", "#.."};
const char* const kPaceMark[] = {"..#..", ".###.", "#####"};

void draw_cell(int16_t x, int16_t y, int16_t w, uint8_t level) {
  constexpr int16_t h = kStationCellSize;
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

void hatch(int16_t x, int16_t y, int16_t w, int16_t h) {
  for (int16_t j = 0; j < h; j++) {
    for (int16_t i = 0; i < w; i++) {
      if (((x + i) + (y + j)) % 4 == 0) g->drawPixel(x + i, y + j, kInk);
    }
  }
}

const MonoBitmap* small_provider_logo(const std::string& id) {
  if (id == "claude") return &usage_logos::kClaude15;
  if (id == "codex") return &usage_logos::kCodex15;
  if (id == "kimi") return &usage_logos::kKimi15;
  if (id == "gemini") return &usage_logos::kGemini15;
  return nullptr;
}

void draw_provider_mark(const std::string& id, int16_t x, int16_t y) {
  if (const MonoBitmap* mark = small_provider_logo(id)) {
    g->drawBitmap(x, y, mark->bits, mark->w, mark->h, kInk);
    return;
  }
  g->drawCircle(x + 7, y + 7, 6, kInk);
  char initial[2] = {id.empty() ? '?' : (char)std::toupper((unsigned char)id[0]),
                     '\0'};
  rlcd_chrome::text(g, initial, x + 7, y + 12, FontRole::Chip, Align::Center);
}

void draw_clock_digits(const std::string& hhmm, int16_t x, int16_t baseline,
                       FontRole role, bool show_colon) {
  const size_t colon_pos = hhmm.find(':');
  if (colon_pos == std::string::npos) {
    rlcd_chrome::text(g, hhmm.c_str(), x, baseline, role);
    return;
  }
  const GFXfont* font = rlcd_chrome::font_for(role);
  const std::string hh = hhmm.substr(0, colon_pos);
  const std::string mm = hhmm.substr(colon_pos + 1);
  int16_t colon_x = x;
  for (unsigned char ch : hh) {
    if (ch >= font->first && ch <= font->last) {
      colon_x += font->glyph[ch - font->first].xAdvance;
    }
  }
  int16_t mm_x = colon_x;
  if (':' >= font->first && ':' <= font->last) {
    mm_x += font->glyph[':' - font->first].xAdvance;
  }
  const int16_t raise_px = role == FontRole::HeroLg ? 6 : 4;
  rlcd_chrome::text(g, hh.c_str(), x, baseline, role);
  rlcd_chrome::text(g, mm.c_str(), mm_x, baseline, role);
  if (show_colon || hhmm == "--:--") {
    rlcd_chrome::text(g, ":", colon_x, baseline - raise_px, role);
  }
}

void draw_masthead(const StationView& view, const Shtc3Reading& env) {
  rlcd_chrome::draw_dotted_text(g, view.header_left, kStationMarginX,
                                kStationHeaderBaseline, FontRole::BodyBold);
  const int16_t left_end =
      kStationMarginX +
      rlcd_chrome::dotted_text_width(g, view.header_left, FontRole::BodyBold);
  rlcd_chrome::draw_ambient_climate_right(g, env, kStationRightX,
                                          kStationHeaderBaseline, left_end + 10,
                                          true);
  g->fillRect(kStationMarginX, kStationHeaderRuleY, 376, 1, kInk);
}

void draw_hero(const StationView& view, const StationFrame& frame) {
  if (view.eco_last_sync) {
    rlcd_chrome::text(g, "LAST SYNC", kStationMarginX,
                      kStationEcoCaptionBaseline, FontRole::Chip);
    draw_clock_digits(view.clock_hhmm, kStationMarginX,
                      kStationEcoClockBaseline, FontRole::HeroMd, true);
  } else {
    draw_clock_digits(view.clock_hhmm, kStationMarginX, kStationHeroBaseline,
                      FontRole::HeroLg, frame.show_colon);
  }

  for (int16_t y = 36; y < 71; y += 4) {
    g->drawPixel(kStationHeroSepX, y, kInk);
  }

  std::string odo = view.total_odo;
  if (frame.sweep < 1.0f && view.total_odo_value > 0) {
    const long double rolled = (long double)view.total_odo_value * frame.sweep;
    const uint64_t val = rolled >= std::numeric_limits<uint64_t>::max()
                             ? std::numeric_limits<uint64_t>::max()
                             : (uint64_t)rolled;
    odo = format_tokens(val);
  }
  rlcd_chrome::text(g, odo.c_str(), kStationOdoX, kStationHeroBaseline,
                    FontRole::HeroLg);

  const int16_t today_val_w =
      rlcd_chrome::text_w(g, view.today_total.c_str(), FontRole::BodyBold);
  rlcd_chrome::text(g, view.today_total.c_str(), kStationRightX,
                    kStationTodayBaseline, FontRole::BodyBold, Align::Right);
  rlcd_chrome::text(
      g, "today",
      (int16_t)(kStationRightX - today_val_w - kStationTodayGap),
      kStationTodayBaseline, FontRole::Body, Align::Right);

  int16_t right_cur = kStationRightX;
  if (!view.window_label.empty()) {
    right_cur -=
        rlcd_chrome::chip(g, view.window_label.c_str(), right_cur,
                          kStationStreakBaseline, Align::Right) +
        kStationWindowChipGap;
  }
  rlcd_chrome::text(g, view.streak_label.c_str(), right_cur,
                    kStationStreakBaseline, FontRole::Body, Align::Right);

  for (int16_t x = kStationMarginX; x < kStationRightX; x += 4) {
    g->drawPixel(x, kStationHeroDividerY, kInk);
  }
}

void draw_heatmap(const StationView& view, const StationFrame& frame) {
  const size_t total_cols = std::min<size_t>(
      view.cols == 0 ? kMonumentMaxCols : view.cols, kMonumentMaxCols);
  int16_t last_month_right = -100;
  for (size_t i = 0; i < view.month_count; i++) {
    const MonumentGridCol mc = monument_grid_col(view.months[i].col, total_cols);
    const int16_t w =
        rlcd_chrome::text_w(g, view.months[i].label, FontRole::Chip);
    const int16_t x = std::min<int16_t>((int16_t)(kStationRightX - w), mc.x);
    if (x >= last_month_right + 8) {
      rlcd_chrome::text(g, view.months[i].label, x, kStationMonthBaseline,
                        FontRole::Chip);
      last_month_right = x + w;
    }
  }

  rlcd_chrome::pixels(g, kGlyphM, kStationMarginX, kStationGridY + 0);
  rlcd_chrome::pixels(g, kGlyphW, kStationMarginX,
                      kStationGridY + 2 * kStationCellPitch);
  rlcd_chrome::pixels(g, kGlyphF, kStationMarginX,
                      kStationGridY + 4 * kStationCellPitch);

  const size_t visible_cols = std::min(
      total_cols, (size_t)std::lround(frame.sweep * total_cols));
  for (size_t col = 0; col < visible_cols; col++) {
    const MonumentGridCol gc = monument_grid_col(col, total_cols);
    for (size_t row = 0; row < kMonumentRows; row++) {
      const uint8_t lvl = view.cells[col * kMonumentRows + row];
      if (lvl == kMonumentCellOutside) continue;
      draw_cell(gc.x, kStationGridY + (int16_t)row * kStationCellPitch, gc.w,
                lvl);
    }
  }

  const int16_t scale_w = rlcd_chrome::text_w(g, "7D", FontRole::Chip);
  const int16_t stop_x = kStationRightX - scale_w - kStationScaleLabelGap;
  rlcd_chrome::text(g, "7D", kStationRightX, kStationScaleLabelBaseline,
                    FontRole::Chip, Align::Right);
  for (int16_t x = kStationMarginX; x < stop_x; x += 4) {
    g->drawPixel(x, kStationGridDividerY, kInk);
  }
}

void draw_pace_bar(const StationRowView& r, int16_t y, float sweep) {
  constexpr int16_t x0 = kStationBarX;
  constexpr int16_t x1 = kStationBarRight;
  if (r.state == UsageRowState::Unavailable || r.car_px < 0) {
    for (int16_t x = x0; x < x1; x += 6) g->fillRect(x, y + 3, 2, 1, kInk);
    return;
  }

  const bool stale = (r.state == UsageRowState::Stale);
  const int16_t car_at = x0 + (int16_t)std::lround(r.car_px * sweep);
  for (int16_t x = x0; x < car_at; x += 4) {
    g->fillRect(x, y + 3, 2, 1, kInk);
  }

  if (x1 > car_at) {
    if (stale) {
      hatch(car_at, y, x1 - car_at, kStationBarH);
      g->fillRect(car_at, y, x1 - car_at, 1, kInk);
      g->fillRect(car_at, y + kStationBarH - 1, x1 - car_at, 1, kInk);
    } else {
      g->fillRect(car_at, y, x1 - car_at, kStationBarH, kInk);
    }
  } else {
    g->drawRect(x1 - 3, y, 3, kStationBarH, kInk);
  }

  if (r.pace_px >= 0) {
    const int16_t px = std::max<int16_t>(
        x0 + 1, std::min<int16_t>(x1 - 2, (int16_t)(x0 + r.pace_px)));
    g->fillRect(px - 1, y, 3, kStationBarH, kPaper);
    g->fillRect(px, y, 1, kStationBarH + 1, kInk);
    rlcd_chrome::pixels(g, kPaceMark, px - 2, y + kStationBarH + 1);
  }
}

void draw_provider_rows(const StationView& view, const StationFrame& frame) {
  if (view.row_count == 0) {
    rlcd_chrome::text(g, "Waiting for first sync", 200,
                      (kStationRowsTop + kStationRowsBottom) / 2 + 5,
                      FontRole::Body, Align::Center);
    return;
  }

  const int16_t first_top = station_row_top(0, view.row_count) + 4;
  const int16_t last_bot =
      station_row_top(view.row_count - 1, view.row_count) +
      station_row_pitch(view.row_count) - 4;
  for (int16_t y = first_top; y < last_bot; y += 4) {
    g->drawPixel(kStationColSepX, y, kInk);
  }

  for (size_t i = 0; i < view.row_count; i++) {
    const StationRowView& r = view.rows[i];
    const int16_t base = station_row_baseline(i, view.row_count);

    draw_provider_mark(r.id, kStationLogoX, base - 13);
    const std::string name = usage_provider_name(r.id);
    rlcd_chrome::text(g, name.c_str(), kStationNameX, base, FontRole::BodyBold);
    if (r.id == "gemini") {
      const int16_t after_name =
          kStationNameX +
          rlcd_chrome::text_w(g, name.c_str(), FontRole::BodyBold);
      g->drawBitmap(after_name + kStationAntigravityGap, base - 12,
                    usage_logos::kAntigravity12.bits,
                    usage_logos::kAntigravity12.w,
                    usage_logos::kAntigravity12.h, kInk);
    }

    if (r.state == UsageRowState::Unavailable) {
      rlcd_chrome::text(g, "--", kStationPctRight, base, FontRole::BodyBold,
                        Align::Right);
    } else {
      const int disp_left =
          (int)std::lround(100.0f - (100 - (int)r.left) * frame.sweep);
      char pct_buf[8];
      std::snprintf(pct_buf, sizeof(pct_buf), "%d%%", disp_left);
      if (r.low_fuel) {
        const int16_t pw =
            rlcd_chrome::text_w(g, pct_buf, FontRole::BodyBold);
        g->fillRect(kStationPctRight - pw - 4, base - 13, pw + 8, 17, kInk);
        rlcd_chrome::text(g, pct_buf, kStationPctRight, base,
                          FontRole::BodyBold, Align::Right, kPaper);
      } else {
        rlcd_chrome::text(g, pct_buf, kStationPctRight, base,
                          FontRole::BodyBold, Align::Right);
      }
    }

    draw_pace_bar(r, base + kStationBarOffsetY, frame.sweep);

    if (!r.chip.empty()) {
      rlcd_chrome::chip(g, r.chip.c_str(), kStationCountdownRight, base,
                        Align::Right);
    } else {
      rlcd_chrome::text(g, r.countdown.c_str(), kStationCountdownRight, base,
                        FontRole::Body, Align::Right);
    }

    if (r.has_token) {
      rlcd_chrome::text(g, r.seven_day.c_str(), kStationTokenRight, base,
                        FontRole::BodyBold, Align::Right);
    } else {
      g->fillRect(kStationTokenRight - kStationDashW, base - 5, kStationDashW,
                  kStationDashH, kInk);
    }
  }
}

}  // namespace

void render_station(const StationView& view, const Shtc3Reading& env,
                    const BatteryReading& batt, RuntimeMode runtime_mode,
                    const StationFrame& frame, bool carousel_pinned) {
  g = display_canvas();
  if (!g) return;
  g->fillSprite(kPaper);
  draw_masthead(view, env);
  draw_hero(view, frame);
  draw_heatmap(view, frame);
  draw_provider_rows(view, frame);
  rlcd_chrome::draw_footer(g, view.footer_chip, view.footer_text, batt,
                           runtime_mode, carousel_pinned);
  display_push();
  g = nullptr;
}

}  // namespace rcpaper
#endif
