// Hand-drawn weekly-allowance "road" view for the 400x300 1 bpp RLCD.
//
// Layout (y grows down): 0-28 headline + rule (+ optional ambient room climate
// on the right), up to four adaptive provider rows (60/78/92 px pitch) centered
// in 29..272, footer baseline 291 below a rule at 272. Each row: 28 px logo,
// remaining percent right-aligned at x=126, name/verdict line, a 132 px fuel
// road from x=164 to the pump (car clamped to the road, pace tick underneath),
// and the reset countdown flush right at x=388.
#ifdef ARDUINO

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "render/canvas.h"
#include "render/page.h"
#include "render/rlcd_chrome.h"
#include "render/usage_layout.h"
#include "render/usage_logos.h"
#include "render/usage_view.h"

namespace rcpaper {
namespace {

using rlcd_chrome::Align;
using rlcd_chrome::FontRole;
using rlcd_chrome::kInk;
using rlcd_chrome::kPaper;

constexpr int16_t kRoadX0 = kUsageRoadX0;
constexpr int16_t kRoadX1 = kUsageRoadX1;
static_assert(kRoadX1 == 296,
              "road, pump, reset icon and the widest countdown must fit in 388 px");

TFT_eSprite* g = nullptr;  // canvas for the frame being drawn

int16_t text_w(const char* s, FontRole role) {
  return rlcd_chrome::text_w(g, s, role);
}

void text(const char* s, int16_t x, int16_t baseline, FontRole role,
          Align align = Align::Left, uint16_t color = kInk) {
  rlcd_chrome::text(g, s, x, baseline, role, align, color);
}

void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color = kInk) {
  if (w > 0 && h > 0) g->fillRect(x, y, w, h, color);
}

void outline(int16_t x, int16_t y, int16_t w, int16_t h) {
  g->drawRect(x, y, w, h, kInk);
}

void hatch(int16_t x, int16_t y, int16_t w, int16_t h) {
  for (int16_t j = 0; j < h; j++)
    for (int16_t i = 0; i < w; i++)
      if (((x + i) + (y + j)) % 4 == 0) g->drawPixel(x + i, y + j, kInk);
}

template <size_t N>
void pix(const char* const (&rows)[N], int16_t x, int16_t y,
         uint16_t color = kInk) {
  rlcd_chrome::pixels(g, rows, x, y, color);
}

void bitmap(const MonoBitmap& b, int16_t x, int16_t y) {
  g->drawBitmap(x, y, b.bits, b.w, b.h, kInk);
}

int16_t chip(const char* s, int16_t x, int16_t baseline, Align align) {
  return rlcd_chrome::chip(g, s, x, baseline, align);
}

// --- sprites ---------------------------------------------------------------
const char* const kDisc[] = {
    "....#####....", "..#########..", ".###########.", ".###########.",
    "#############", "#############", "#############", "#############",
    "#############", ".###########.", ".###########.", "..#########..",
    "....#####....",
};
const char* const kDrop[] = {".#", "##", "##"};
const char* const kClockIcon[] = {
    "..#####..", ".#.....#.", "#...#...#", "#...#...#", "#...###.#",
    "#.......#", "#.......#", ".#.....#.", "..#####..",
};
const char* const kRing1[] = {".#.", "#.#", ".#."};
const char* const kRing2[] = {".##.", "#..#", "#..#", ".##."};
const char* const kWheel[] = {".###.", "#####", "##.##", "#####", ".###."};
// End of the road: reaching the pump means the allowance is used up.
const char* const kPump[] = {
    "#######...", "#.....#.#.", "#.....#..#", "#.....#..#", "#######..#",
    "#######..#", "#######.##", "#######.#.", "#######...", "#######...",
    "#######...", "#######...", "#########.",
};
// Even pace: where the car would be if the week's allowance were spread evenly.
const char* const kPaceMark[] = {"..#..", ".###.", "#####"};

struct Px { int8_t x, y; };
// Paper pixels punched out of kDisc for each mood.
const Px kOpen[] = {{4,4},{4,5},{8,4},{8,5},{3,8},{9,8},{4,9},{5,9},{6,9},{7,9},{8,9},{5,10},{6,10},{7,10}};
const Px kComfy[] = {{4,4},{4,5},{8,4},{8,5},{4,8},{8,8},{5,9},{6,9},{7,9}};
const Px kPace[] = {{4,4},{4,5},{8,4},{8,5},{4,9},{5,9},{6,9},{7,9},{8,9}};
const Px kHot[] = {{4,4},{4,5},{8,4},{8,5},{4,9},{5,8},{6,9},{7,8},{8,9}};
const Px kLow[] = {{4,4},{3,5},{8,4},{9,5},{5,9},{6,9},{7,9},{4,10},{8,10}};
const Px kNap[] = {{3,5},{4,5},{5,5},{7,5},{8,5},{9,5},{6,9}};

template <size_t N>
void punch(const Px (&pts)[N], int16_t x, int16_t y) {
  for (const Px& p : pts) g->drawPixel(x + p.x, y + p.y, kPaper);
}

void face(UsageMood mood, int16_t x, int16_t y) {
  if (mood == UsageMood::None) return;
  pix(kDisc, x, y);
  switch (mood) {
    case UsageMood::Open: punch(kOpen, x, y); break;
    case UsageMood::Comfy: punch(kComfy, x, y); break;
    case UsageMood::Pace: punch(kPace, x, y); break;
    case UsageMood::Hot: punch(kHot, x, y); pix(kDrop, x + 13, y); break;
    case UsageMood::Low: punch(kLow, x, y); break;
    case UsageMood::Nap: punch(kNap, x, y); break;
    case UsageMood::None: break;
  }
}

void car(int16_t x, int16_t y, bool ghost) {
  if (ghost) {  // stale data: an outline of the car
    outline(x, y + 5, 22, 4);
    outline(x + 6, y + 1, 11, 4);
    rect(x + 7, y, 9, 1);
    pix(kRing2, x + 3, y + 8);
    pix(kRing2, x + 15, y + 8);
    return;
  }
  rect(x + 7, y, 9, 1);
  rect(x + 6, y + 1, 11, 4);
  rect(x + 8, y + 2, 3, 2, kPaper);
  rect(x + 12, y + 2, 3, 2, kPaper);
  rect(x, y + 5, 22, 4);
  g->drawPixel(x, y + 5, kPaper);
  g->drawPixel(x + 21, y + 5, kPaper);
  rect(x + 2, y + 8, 7, 1, kPaper);
  rect(x + 13, y + 8, 7, 1, kPaper);
  pix(kWheel, x + 3, y + 8);
  pix(kWheel, x + 14, y + 8);
}

void zz(int16_t x, int16_t y, const UsageFrame& f) {
  const int16_t bob =
      f.moving ? (int16_t)lroundf(sinf(f.t_ms / 1000.0f * 2.5f) * 1.5f) : 0;
  text("z", x, y + bob, FontRole::BodyBold);
  text("Z", x + 7, y - 6 + bob, FontRole::BodyBold);
}

// --- chrome ------------------------------------------------------------------
const MonoBitmap* row_logo(const std::string& id) {
  if (id == "claude") return &usage_logos::kClaude28;
  if (id == "codex") return &usage_logos::kCodex28;
  if (id == "kimi") return &usage_logos::kKimi28;
  if (id == "gemini") return &usage_logos::kGemini28;
  return nullptr;
}
const MonoBitmap* pick_logo(const std::string& id) {
  if (id == "claude") return &usage_logos::kClaude15;
  if (id == "codex") return &usage_logos::kCodex15;
  if (id == "kimi") return &usage_logos::kKimi15;
  if (id == "gemini") return &usage_logos::kGemini15;
  return nullptr;
}

// Accounts without a baked mark get a disc with their initial.
void generic_logo(const std::string& id, int16_t x, int16_t y, int16_t d,
                  FontRole role) {
  g->drawCircle(x + d / 2, y + d / 2, d / 2 - 1, kInk);
  char initial[2] = {
      id.empty() ? '?' : (char)toupper((unsigned char)id[0]), '\0'};
  text(initial, x + d / 2, y + d / 2 + (role == FontRole::Name ? 8 : 5), role,
       Align::Center);
}
void logo(const std::string& id, int16_t x, int16_t y) {
  const MonoBitmap* mark = row_logo(id);
  if (mark) bitmap(*mark, x, y);
  else generic_logo(id, x, y, 28, FontRole::Name);
}
void small_logo(const std::string& id, int16_t x, int16_t y) {
  const MonoBitmap* mark = pick_logo(id);
  if (mark) bitmap(*mark, x, y);
  else generic_logo(id, x, y, 15, FontRole::BodyBold);
}

void headline(const UsageView& v, const Shtc3Reading& env,
              const UsageFrame& f) {
  int16_t x = kUsageMarginX;
  switch (v.headline_icon) {
    case UsageIcon::Spark:
      if (f.moving && (f.tick / 4) % 2) pix(rlcd_chrome::kSparkSmall, x, 8);
      else pix(rlcd_chrome::kSpark, x, 8);
      x += 15;
      break;
    case UsageIcon::Clock:
      pix(kClockIcon, x, 9);
      x += 14;
      break;
    case UsageIcon::Face:
      face(v.headline_face, x, 7);
      x += 18;
      break;
    case UsageIcon::None:
      break;
  }
  text(v.headline.c_str(), x, kUsageHeaderBaseline, FontRole::Body);
  int16_t x_end = x + text_w(v.headline.c_str(), FontRole::Body);
  if (v.pick >= 0 && v.pick < (int)v.count) {
    const int16_t pick_x = x_end + 6;
    const std::string pick_name = usage_provider_name(v.rows[v.pick].id);
    small_logo(v.rows[v.pick].id, pick_x, 6);
    text(pick_name.c_str(), pick_x + 19, kUsageHeaderBaseline,
         FontRole::BodyBold);
    x_end = pick_x + 19 + text_w(pick_name.c_str(), FontRole::BodyBold);
  }
  rlcd_chrome::draw_ambient_climate_right(g, env, kUsageRightX,
                                          kUsageHeaderBaseline, x_end + 10,
                                          true);
  rect(kUsageMarginX, kUsageHeaderRuleY, 376, 1);
}

// --- rows --------------------------------------------------------------------
void number(const UsageRowView& r, int16_t right, int16_t baseline,
            int16_t box_top, int16_t box_h, float sweep) {
  if (r.state == UsageRowState::Unavailable) {
    text("--", right + 20, baseline - 4, FontRole::HeroLg, Align::Right);
    return;
  }
  char s[8];  // the sweep burns down from a full tank while the car drives in
  std::snprintf(s, sizeof(s), "%d",
                (int)lroundf(100.0f - (100 - r.left) * sweep));
  const int16_t nw = text_w(s, FontRole::HeroLg);
  const int16_t pw = text_w("%", FontRole::Name);
  const bool low = r.left <= 10;
  const uint16_t color = low ? kPaper : kInk;
  if (low) rect(right - nw - 6, box_top, nw + pw + 14, box_h);
  text(s, right, baseline, FontRole::HeroLg, Align::Right, color);
  text("%", right + 2, baseline, FontRole::Name, Align::Left, color);
}

void road(const UsageRowView& r, int16_t y, const UsageFrame& f) {
  const int16_t x0 = kRoadX0, x1 = kRoadX1;
  pix(kPump, kUsagePumpX, y - 6);  // base level with the road's bottom edge
  if (r.state == UsageRowState::Unavailable || r.car_px < 0) {
    for (int16_t x = x0; x < x1; x += 6) rect(x, y + 3, 2, 1);
    return;
  }
  const bool stale = r.state == UsageRowState::Stale;
  const bool moving = f.moving && !stale && r.mood != UsageMood::Nap;
  const int16_t car_at = x0 + (int16_t)lroundf(
      ((float)r.car_px * kUsageDrawRoadPx / kUsageRoadPx) * f.sweep);
  for (int16_t x = x0; x < car_at; x += 4) rect(x, y + 3, 2, 1);  // allowance already burned
  if (x1 > car_at) {  // what is left
    if (stale) {
      hatch(car_at, y, x1 - car_at, 7);
      rect(car_at, y, x1 - car_at, 1);
      rect(car_at, y + 6, x1 - car_at, 1);
    } else {
      rect(car_at, y, x1 - car_at, 7);
      const int phase = moving ? (int)((f.tick * 3) % 10) : 0;  // 3 px per frame
      for (int16_t x = car_at + 2; x < x1 - 2; x++)
        if ((x - car_at + phase) % 10 < 5) g->drawPixel(x, y + 3, kPaper);
    }
  }
  if (r.pace_px >= 0) {  // cut through the road so the tick reads on solid, hatch and dots
    const int16_t px = x0 + (int16_t)lroundf((float)r.pace_px * kUsageDrawRoadPx / kUsageRoadPx);
    rect(px - 1, y, 3, 7, kPaper);
    rect(px, y, 1, 8);
    pix(kPaceMark, px - 2, y + 8);
  }
  // Keep the car strictly within [x0, x1 - 22] so it never intrudes into the % column.
  const int16_t car_x = std::max<int16_t>(x0, std::min<int16_t>(car_at - 11, x1 - kUsageCarW));
  const int16_t bob = moving ? (int16_t)(f.tick % 2) : 0;
  car(car_x, y + kUsageCarOffsetY - bob, stale);
  if (moving) {
    const int k = (int)((f.tick / 2) % 2);
    pix(kRing1, car_x - 5 - k * 2, y - 6 - k);
    if (k) pix(kRing2, car_x - 11, y - 9);
  }
  if (r.mood == UsageMood::Nap) zz(car_x - 17, y - 1, f);
}

void row(size_t i, const UsageRowView& r, size_t count, const UsageFrame& f) {
  const UsageRowMetrics m = usage_row_metrics(count);
  const int16_t top = usage_row_top(i, count);
  if (i > 0) for (int16_t x = kUsageMarginX; x < kUsageRightX; x += 4) g->drawPixel(x, top, kInk);
  logo(r.id, kUsageLogoX, top + m.logo_y);
  number(r, kUsageNumRight, top + m.num_base, top + m.low_box_top, m.low_box_h, f.sweep);
  const int16_t name_base = top + m.name_base;
  const int16_t road_y = top + m.road_y;
  const std::string name = usage_provider_name(r.id);
  text(name.c_str(), kRoadX0, name_base, FontRole::Name);
  if (r.id == "gemini") {  // Antigravity's Gemini weekly pool
    bitmap(usage_logos::kAntigravity12,
           kRoadX0 + text_w(name.c_str(), FontRole::Name) + 6,
           name_base - 12);
  }
  if (!r.chip.empty()) {
    chip(r.chip.c_str(), kUsageRightX, name_base, Align::Right);
  } else if (!r.verdict.empty() && r.state != UsageRowState::Stale) {
    text(r.verdict.c_str(), kUsageRightX, name_base,
         r.runs_dry ? FontRole::BodyBold : FontRole::Body, Align::Right);
  }
  road(r, road_y, f);
  text(r.countdown.c_str(), kUsageRightX, road_y + 7, FontRole::Body, Align::Right);
  // Clock before the countdown: time until the allowance refills.
  if (r.countdown != "--") {
    pix(kClockIcon,
        kUsageRightX - text_w(r.countdown.c_str(), FontRole::Body) -
            (kUsageClockW + kUsageClockGap),
        road_y - 3);
  }
}

}  // namespace

void render_usage(const UsageView& view, const Shtc3Reading& env,
                  const BatteryReading& batt, RuntimeMode runtime_mode,
                  const UsageFrame& frame, bool carousel_pinned) {
  g = display_canvas();
  if (!g) return;
  g->fillSprite(kPaper);
  headline(view, env, frame);
  for (size_t i = 0; i < view.count; i++) row(i, view.rows[i], view.count, frame);
  rlcd_chrome::draw_footer(g, view.footer_chip, view.footer_text, batt,
                           runtime_mode, carousel_pinned);
  display_push();
  g = nullptr;
}

}  // namespace rcpaper

#endif  // ARDUINO
