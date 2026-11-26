// Hand-drawn Page 1 ("desk-station"): Monument Spine home overview combining
// Desk Clock / ECO Last Sync, Total Odometer, split rolling Heatmap Window +
// multi-scale Token Skyline with 4-phase local-RTC sky, Ambient Desk Climate,
// 1..4 Provider Allowance + 7D Token strips, and 3-ledge Desk Cat on 400x300 1bpp.
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
#include "render/desk_cat.h"
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

// Sky sprites matching the approved 400x300 1-bit prototype.
const char* const kSpriteSun[] = {
    "   #   ",
    " # # # ",
    "  ###  ",
    "#######",
    "  ###  ",
    " # # # ",
    "   #   ",
};
const char* const kSpriteMoon[] = {
    "  ### ",
    " #### ",
    "###   ",
    "###   ",
    " #### ",
    "  ### ",
};
const char* const kSpriteCloud1[] = {
    "  ###   ",
    " ###### ",
    "########",
};
const char* const kSpriteCloud2[] = {
    " ###  ",
    "######",
};

// Sleek 15x10 Pixel Cat Sprites.
const char* const kCatWalk1[] = {
    "            # #",
    "#          ####",
    "#  ####### ####",
    " ######### ####",
    "  #############",
    "   ########### ",
    "   ##########  ",
    "   ##  ##  ##  ",
    "  ##       ##  ",
    "  #         #  ",
};
const char* const kCatWalk2[] = {
    "            # #",
    " #         ####",
    " # ####### ####",
    "  ######## ####",
    "  #############",
    "   ########### ",
    "   ##########  ",
    "    ## ## ##   ",
    "    ##   ##    ",
    "    #     #    ",
};
const char* const kCatSit1[] = {
    "       # #     ",
    "      #####    ",
    "      #####    ",
    "     #######   ",
    "    ########   ",
    "#   ########   ",
    "#  #########   ",
    "#  #########   ",
    " ## #######    ",
    "   #########   ",
};
const char* const kCatSit2[] = {
    "       # #     ",
    "      #####    ",
    "      #####    ",
    "     #######   ",
    "    ########   ",
    "   #########   ",
    " # #########   ",
    "#  #########   ",
    " ## #######    ",
    "   #########   ",
};
const char* const kCatPounce[] = {
    " #          # #",
    "#          ####",
    " # ####### ####",
    "  #############",
    "   ########### ",
    "  ##       ### ",
    " ##          ##",
    " #            #",
    "               ",
    "               ",
};
const char* const kCatJump[] = {
    "            # #",
    "           ####",
    "#   ###### ####",
    " ##############",
    "  ###########  ",
    " ##        ##  ",
    "#           #  ",
    "               ",
    "               ",
    "               ",
};
const char* const kCatSleep1[] = {
    "               ",
    "               ",
    "               ",
    "     # #       ",
    "   #######     ",
    "  ##########   ",
    " ############  ",
    "#############  ",
    "############## ",
    " ############  ",
};
const char* const kCatSleep2[] = {
    "               ",
    "               ",
    "               ",
    "     # #       ",
    "   #######     ",
    "  ##########   ",
    " ############# ",
    "############## ",
    "############## ",
    " ############  ",
};
const char* const kSpriteYarn[] = {
    " ### ",
    "##.##",
    "#.###",
    "###.#",
    " ### ",
};
const char* const kGlyphZ[] = {
    "###",
    "  #",
    " # ",
    "#  ",
    "###",
};

void draw_px(int16_t x, int16_t y, uint16_t color = kInk) {
  if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
    g->drawPixel(x, y, color);
  }
}

void draw_sprite_rows(const char* const* rows, int16_t h, int16_t w, int16_t x,
                      int16_t y, bool flip_x = false, uint16_t color = kInk,
                      bool clear_dot = false) {
  for (int16_t j = 0; j < h; j++) {
    for (int16_t i = 0; i < w; i++) {
      const char ch = rows[j][flip_x ? (w - 1 - i) : i];
      if (ch == '#') draw_px((int16_t)(x + i), (int16_t)(y + j), color);
      else if (clear_dot && ch == '.') draw_px((int16_t)(x + i), (int16_t)(y + j), kPaper);
    }
  }
}

void draw_clipped_cloud(const char* const* rows, int16_t h, int16_t w,
                        int16_t rel_x, int16_t y) {
  for (int16_t j = 0; j < h; j++) {
    for (int16_t i = 0; i < w; i++) {
      const int16_t xx = (int16_t)(kStationSkylineX + rel_x + i);
      if (xx >= kStationSkylineX && xx < kStationSkylineRight &&
          rows[j][i] == '#') {
        draw_px(xx, (int16_t)(y + j), kInk);
      }
    }
  }
}

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
  if (id == "zai") return &usage_logos::kZai15;
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

void draw_skyline_chip(const std::string& label, bool filled) {
  const int16_t tw = rlcd_chrome::text_w(g, label.c_str(), FontRole::Chip);
  const int16_t cw = (int16_t)(tw + kStationChipExtraW);
  const int16_t cx = (int16_t)(kStationSkylineRight - cw);
  const int16_t cy =
      (int16_t)(kStationSkylineHeaderBaseline + kStationSkylineChipTopOffset);
  if (filled) {
    g->fillRect(cx, cy, cw, kStationSkylineChipH, kInk);
    rlcd_chrome::text(g, label.c_str(), (int16_t)(cx + 4),
                      kStationSkylineHeaderBaseline, FontRole::Chip,
                      Align::Left, kPaper);
  } else {
    g->drawRect(cx, cy, cw, kStationSkylineChipH, kInk);
    rlcd_chrome::text(g, label.c_str(), (int16_t)(cx + 4),
                      kStationSkylineHeaderBaseline, FontRole::Chip,
                      Align::Left, kInk);
  }
}

void draw_token_skyline(const StationView& view, const StationFrame& frame) {
  const TokenSkylineView& s = view.skyline;
  rlcd_chrome::text(g, s.label.c_str(), kStationSkylineX,
                    kStationSkylineHeaderBaseline, FontRole::Chip);
  draw_skyline_chip(s.delta_label, s.has_delta && s.delta_positive);

  const SkyVisualState sky =
      derive_sky_visual(view.minutes_of_day, frame.animating, frame.tick);

  // 1. Real-time 4-phase local-RTC sky.
  if (sky.phase == SkyPhase::Night) {
    draw_sprite_rows(kSpriteMoon, 6, 6, sky.body_x, sky.body_y);
    static constexpr int16_t kStars[][2] = {
        {286, 96}, {304, 101}, {323, 95}, {347, 99}, {366, 94}, {379, 102},
    };
    for (size_t idx = 0; idx < 6; idx++) {
      const int16_t sx = kStars[idx][0];
      const int16_t sy = kStars[idx][1];
      if (std::abs(sx - (sky.body_x + 3)) < 7 &&
          std::abs(sy - (sky.body_y + 3)) < 7) {
        continue;
      }
      const uint8_t tw = frame.animating
                             ? (uint8_t)((sky.twinkle_phase + idx) % 4u)
                             : (uint8_t)(idx % 2u);
      draw_px(sx, sy, kInk);
      if (tw == 0) {
        draw_px((int16_t)(sx - 1), sy, kInk);
        draw_px((int16_t)(sx + 1), sy, kInk);
        draw_px(sx, (int16_t)(sy - 1), kInk);
        draw_px(sx, (int16_t)(sy + 1), kInk);
      }
    }
  } else {
    draw_sprite_rows(kSpriteSun, 7, 7, sky.body_x, sky.body_y);
    if (sky.phase == SkyPhase::Dawn || sky.phase == SkyPhase::Dusk) {
      for (int16_t hx = kStationSkylineX; hx < kStationSkylineRight; hx += 4) {
        draw_px(hx, (int16_t)(kStationSkylineTop + 17), kInk);
      }
      for (int16_t hx = kStationSkylineX + 2; hx < kStationSkylineRight;
           hx += 6) {
        draw_px(hx, (int16_t)(kStationSkylineTop + 15), kInk);
      }
    }
    draw_clipped_cloud(kSpriteCloud1, 3, 8, sky.cloud1_x,
                       (int16_t)(kStationSkylineTop + 2));
    if (sky.phase == SkyPhase::Day) {
      draw_clipped_cloud(kSpriteCloud2, 2, 6, sky.cloud2_x,
                         (int16_t)(kStationSkylineTop + 9));
    }
  }

  // 2. Prior-period dashed horizon baseline.
  const int16_t avg_y = (int16_t)(kStationSkylineGroundY - s.baseline_px);
  if (s.baseline_px > 0) {
    for (int16_t x = kStationSkylineX; x < kStationSkylineRight; x += 3) {
      draw_px(x, avg_y, kInk);
    }
  }

  // 3. Skyscraper bars (8W: 8x11+3, 7D: 7x12+4, 6M: 6x15+4).
  const size_t n = std::min<size_t>(s.bar_count, kSkylineMaxBars);
  const StationSkylineBarMetrics bm = station_skyline_bar_metrics(n);
  const bool lit_night =
      (sky.phase == SkyPhase::Night || sky.phase == SkyPhase::Dusk);
  const size_t visible =
      std::min(n, (size_t)std::lround(frame.sweep * (float)n));

  for (size_t i = 0; i < visible; i++) {
    const int16_t bh = (int16_t)s.bar_heights[i];
    if (bh <= 0) continue;
    const int16_t bx = station_skyline_bar_x(i, n);
    const int16_t by = (int16_t)(kStationSkylineGroundY - bh);
    const bool is_latest = (i + 1 == n);

    g->fillRect(bx, by, bm.bar_w, bh, kPaper);

    if (lit_night) {
      g->fillRect(bx, by, bm.bar_w, bh, kInk);
      if (i % 3 == 1 && bh >= 10) {
        g->fillRect((int16_t)(bx + 2), (int16_t)(by - 2),
                    (int16_t)(bm.bar_w - 4), 2, kInk);
      }
      for (int16_t wy = (int16_t)(by + 3); wy <= kStationSkylineGroundY - 4;
           wy += 4) {
        for (int16_t wx = (int16_t)(bx + 2); wx <= bx + bm.bar_w - 4; wx += 3) {
          const int hash = ((int)i * 31 + wy * 17 + wx * 13) % 10;
          const bool lit =
              sky.phase == SkyPhase::Night ? (hash < 7) : (hash < 4);
          if (lit) g->fillRect(wx, wy, 2, 2, kPaper);
        }
      }
    } else {
      g->drawRect(bx, by, bm.bar_w, bh, kInk);
      g->fillRect(bx, by, bm.bar_w, 2, kInk);
      if (i % 3 == 1 && bh >= 10) {
        g->drawRect((int16_t)(bx + 2), (int16_t)(by - 2),
                    (int16_t)(bm.bar_w - 4), 3, kInk);
      }
      for (int16_t wy = (int16_t)(by + 4); wy <= kStationSkylineGroundY - 3;
           wy += 4) {
        for (int16_t wx = (int16_t)(bx + 2); wx <= bx + bm.bar_w - 3; wx += 3) {
          draw_px(wx, wy, kInk);
          if (s.above_baseline[i]) draw_px((int16_t)(wx + 1), wy, kInk);
        }
      }
    }

    if (is_latest) {
      const int16_t mid = (int16_t)(bx + bm.bar_w / 2 - 1);
      g->fillRect(mid, (int16_t)(by - 5), 2, 5, kInk);
      if (sky.beacon_on) {
        g->fillRect(mid, (int16_t)(by - 7), 2, 2, kInk);
      }
      rlcd_chrome::pixels(g, kPaceMark, (int16_t)(mid - 1),
                          kStationSkylineMarkerY);
    }

    if (lit_night && s.baseline_px > 0 && by < avg_y &&
        avg_y < kStationSkylineGroundY) {
      for (int16_t xx = (int16_t)(bx + 1); xx < bx + bm.bar_w - 1; xx += 3) {
        draw_px(xx, avg_y, kPaper);
      }
    }
  }

  g->fillRect(kStationSkylineX, kStationSkylineGroundY, kStationSkylineW, 1,
              kInk);
}

void draw_middle_spine(const StationView& view, const StationFrame& frame) {
  const size_t total_cols = std::min<size_t>(
      view.cols == 0 ? kMonumentGridCols6M : view.cols, kMonumentMaxCols);
  int16_t last_month_right = -100;
  for (size_t i = 0; i < view.month_count; i++) {
    const MonumentGridCol mc = station_grid_col(view.months[i].col, total_cols);
    const int16_t w =
        rlcd_chrome::text_w(g, view.months[i].label, FontRole::Chip);
    const int16_t x = std::min<int16_t>((int16_t)(kStationGridRight - w), mc.x);
    if (x >= last_month_right + 8) {
      rlcd_chrome::text(g, view.months[i].label, x, kStationMonthBaseline,
                        FontRole::Chip);
      last_month_right = (int16_t)(x + w);
    }
  }

  rlcd_chrome::pixels(g, kGlyphM, kStationMarginX, kStationGridY + 0);
  rlcd_chrome::pixels(g, kGlyphW, kStationMarginX,
                      kStationGridY + 2 * kStationCellPitch);
  rlcd_chrome::pixels(g, kGlyphF, kStationMarginX,
                      kStationGridY + 4 * kStationCellPitch);

  const size_t visible_cols = std::min(
      total_cols, (size_t)std::lround(frame.sweep * (float)total_cols));
  for (size_t col = 0; col < visible_cols; col++) {
    const MonumentGridCol gc = station_grid_col(col, total_cols);
    for (size_t row = 0; row < kMonumentRows; row++) {
      const uint8_t lvl = view.cells[col * kMonumentRows + row];
      if (lvl == kMonumentCellOutside) continue;
      draw_cell(gc.x, (int16_t)(kStationGridY + (int16_t)row * kStationCellPitch),
                gc.w, lvl);
    }
  }

  // Vertical dotted divider between left heatmap and right Token Skyline.
  for (int16_t y = kStationSpineDividerTop; y < kStationSpineDividerBottom;
       y += 4) {
    draw_px(kStationSpineDividerX, y, kInk);
  }

  draw_token_skyline(view, frame);

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

void draw_desk_cat(const StationFrame& frame) {
  if (!frame.cat_enabled) return;
  const DeskCatState cat = desk_cat_render_pose(frame.cat, frame.animating);
  const int16_t cx = (int16_t)std::lround(cat.x);
  const int16_t cy = (int16_t)std::lround(cat.y);
  const int16_t top_y = (int16_t)(cy - kDeskCatH);
  const bool flip_x = (cat.dir < 0);

  const char* const* sprite = kCatSit1;
  if (!frame.animating) {
    sprite = kCatSit1;
  } else if (cat.mode == DeskCatMode::Jump) {
    sprite = kCatJump;
  } else if (cat.mode == DeskCatMode::Walk) {
    sprite = ((frame.tick / 2u) % 2u == 0) ? kCatWalk1 : kCatWalk2;
  } else if (cat.mode == DeskCatMode::PlayYarn) {
    const float paw_x = cat.x + (cat.dir > 0 ? (float)kDeskCatW : 0.0f);
    const bool close = std::fabs(cat.yarn_x - paw_x) < 14.0f;
    sprite = close ? kCatPounce
                   : ((frame.tick % 2u == 0) ? kCatWalk1 : kCatWalk2);
  } else if (cat.mode == DeskCatMode::Sit) {
    sprite = ((frame.tick / 4u) % 2u == 0) ? kCatSit1 : kCatSit2;
  } else if (cat.mode == DeskCatMode::Sleep) {
    sprite = ((frame.tick / 6u) % 2u == 0) ? kCatSleep1 : kCatSleep2;
  }

  // 1 px paper halo while jumping across page content so silhouette stays crisp.
  if (frame.animating && cat.mode == DeskCatMode::Jump) {
    for (int16_t j = 0; j < kDeskCatH; j++) {
      for (int16_t i = 0; i < kDeskCatW; i++) {
        const char ch = sprite[j][flip_x ? (kDeskCatW - 1 - i) : i];
        if (ch == '#') {
          for (int16_t dy = -1; dy <= 1; dy++) {
            for (int16_t dx = -1; dx <= 1; dx++) {
              draw_px((int16_t)(cx + i + dx), (int16_t)(top_y + j + dy), kPaper);
            }
          }
        }
      }
    }
  } else {
    g->fillRect((int16_t)(cx - 1), top_y, (int16_t)(kDeskCatW + 2), kDeskCatH,
                kPaper);
  }
  draw_sprite_rows(sprite, kDeskCatH, kDeskCatW, cx, top_y, flip_x, kInk);

  if (frame.animating && cat.mode == DeskCatMode::Sleep) {
    const int16_t z_phase = (int16_t)((frame.tick / 4u) % 3u);
    draw_sprite_rows(kGlyphZ, 5, 3, (int16_t)(cx + 13),
                     (int16_t)(top_y - 1 - z_phase), false, kInk);
  }

  if (frame.animating && cat.yarn_active && cat.mode == DeskCatMode::PlayYarn) {
    const int16_t yx = (int16_t)std::lround(cat.yarn_x);
    const int16_t yy = (int16_t)(cy - kDeskCatYarnSize);
    g->fillRect((int16_t)(yx - 1), (int16_t)(yy - 1),
                (int16_t)(kDeskCatYarnSize + 2),
                (int16_t)(kDeskCatYarnSize + 1), kPaper);
    draw_sprite_rows(kSpriteYarn, kDeskCatYarnSize, kDeskCatYarnSize, yx, yy,
                     false, kInk, true);
    draw_px((int16_t)(yx - (cat.yarn_vx >= 0.0f ? 1 : -5)), (int16_t)(cy - 1),
            kInk);
    draw_px((int16_t)(yx - (cat.yarn_vx >= 0.0f ? 2 : -6)), (int16_t)(cy - 2),
            kInk);
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
  draw_middle_spine(view, frame);
  draw_provider_rows(view, frame);
  rlcd_chrome::draw_footer(g, view.footer_chip, view.footer_text, batt,
                           runtime_mode, carousel_pinned);
  draw_desk_cat(frame);
  display_push();
  g = nullptr;
}

}  // namespace rcpaper
#endif
