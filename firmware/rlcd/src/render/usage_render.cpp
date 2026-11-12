// Hand-drawn weekly-allowance "road" view for the 400x300 1 bpp RLCD.
//
// Layout (y grows down):  0-24 headline + rule, four 62 px provider rows from
// y=26, 275 footer rule, footer baseline 294. Each row: 28 px logo, remaining
// percent right-aligned at x=128, name/verdict line, a 150 px fuel road from
// x=162 to the pump (car at the used share, pace tick underneath), and the
// reset countdown at the right edge.
#ifdef ARDUINO

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "render/canvas.h"
#include "render/page.h"
#include "render/usage_logos.h"
#include "render/usage_view.h"

namespace rcpaper {
namespace {

constexpr uint16_t kInk = 0x0000;
constexpr uint16_t kPaper = 0xFFFF;
constexpr int16_t kRoadX0 = 162;
constexpr int16_t kRoadX1 = kRoadX0 + kUsageRoadPx;
static_assert(kRoadX1 == 312, "road, pump, reset icon and the widest countdown must fit in 388 px");

TFT_eSprite* g = nullptr;  // canvas for the frame being drawn

enum class Align : uint8_t { Left, Right, Center };

const GFXfont* font(int pt, bool bold) {
  if (pt >= 24) return &FreeSansBold24pt7b;
  if (pt >= 12) return bold ? &FreeSansBold12pt7b : &FreeSans12pt7b;
  return bold ? &FreeSansBold9pt7b : &FreeSans9pt7b;
}

int16_t text_w(const char* s, int pt, bool bold = false) {
  g->setFreeFont(font(pt, bold));
  g->setTextSize(1);
  return g->textWidth(s);
}

void text(const char* s, int16_t x, int16_t baseline, int pt, bool bold = false,
          Align align = Align::Left, uint16_t color = kInk) {
  g->setFreeFont(font(pt, bold));
  g->setTextSize(1);
  g->setTextWrap(false, false);
  g->setTextDatum(align == Align::Left ? L_BASELINE
                  : align == Align::Right ? R_BASELINE : C_BASELINE);
  g->setTextColor(color);  // fg only: transparent background for GFX fonts
  g->drawString(s, x, baseline);
}

void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color = kInk) {
  if (w > 0 && h > 0) g->fillRect(x, y, w, h, color);
}

void outline(int16_t x, int16_t y, int16_t w, int16_t h) { g->drawRect(x, y, w, h, kInk); }

void hatch(int16_t x, int16_t y, int16_t w, int16_t h) {
  for (int16_t j = 0; j < h; j++)
    for (int16_t i = 0; i < w; i++)
      if (((x + i) + (y + j)) % 4 == 0) g->drawPixel(x + i, y + j, kInk);
}

template <size_t N>
void pix(const char* const (&rows)[N], int16_t x, int16_t y, uint16_t color = kInk) {
  for (size_t j = 0; j < N; j++)
    for (size_t i = 0; rows[j][i]; i++)
      if (rows[j][i] == '#') g->drawPixel(x + (int16_t)i, y + (int16_t)j, color);
}

void bitmap(const MonoBitmap& b, int16_t x, int16_t y) {
  g->drawBitmap(x, y, b.bits, b.w, b.h, kInk);
}

// Inverted label; returns its width.
int16_t chip(const char* s, int16_t x, int16_t baseline, Align align) {
  const int16_t w = text_w(s, 9, true) + 10;
  const int16_t x0 = align == Align::Right ? x - w : align == Align::Center ? x - w / 2 : x;
  rect(x0, baseline - 15, w, 19);
  text(s, x0 + 5, baseline, 9, true, Align::Left, kPaper);
  return w;
}

// --- sprites ---------------------------------------------------------------
const char* const kDisc[] = {
    "....#####....", "..#########..", ".###########.", ".###########.",
    "#############", "#############", "#############", "#############",
    "#############", ".###########.", ".###########.", "..#########..",
    "....#####....",
};
const char* const kDrop[] = {".#", "##", "##"};
const char* const kSpark[] = {
    "....#....", "....#....", "....#....", "...###...", "#########",
    "...###...", "....#....", "....#....", "....#....",
};
const char* const kSparkSmall[] = {
    ".........", ".........", "....#....", "...###...", "..#####..",
    "...###...", "....#....", ".........", ".........",
};
const char* const kClockIcon[] = {
    "..#####..", ".#.....#.", "#...#...#", "#...#...#", "#...###.#",
    "#.......#", "#.......#", ".#.....#.", "..#####..",
};
const char* const kXMark[] = {"##...##", ".##.##.", "..###..", ".##.##.", "##...##"};
const char* const kRing1[] = {".#.", "#.#", ".#."};
const char* const kRing2[] = {".##.", "#..#", "#..#", ".##."};
const char* const kWheel[] = {".###.", "#####", "##.##", "#####", ".###."};
// End of the road: reaching the pump means the allowance is used up.
const char* const kPump[] = {
    "#######...", "#.....#.#.", "#.....#..#", "#.....#..#", "#######..#", "#######..#", "#######.##",
    "#######.#.", "#######...", "#######...", "#######...", "#######...", "#########.",
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
  const int16_t bob = f.moving ? (int16_t)lroundf(sinf(f.t_ms / 1000.0f * 2.5f) * 1.5f) : 0;
  text("z", x, y + bob, 9, true);
  text("Z", x + 7, y - 6 + bob, 9, true);
}

// --- chrome ------------------------------------------------------------------
const MonoBitmap* const kRowLogos[4] = {
    &usage_logos::kClaude28, &usage_logos::kCodex28, &usage_logos::kKimi28, &usage_logos::kGemini28};
const MonoBitmap* const kPickLogos[4] = {
    &usage_logos::kClaude15, &usage_logos::kCodex15, &usage_logos::kKimi15, &usage_logos::kGemini15};

void headline(const UsageView& v, const UsageFrame& f) {
  int16_t x = 12;
  switch (v.headline_icon) {
    case UsageIcon::Spark:
      if (f.moving && (f.tick / 4) % 2) pix(kSparkSmall, x, 6);
      else pix(kSpark, x, 6);
      x += 14;
      break;
    case UsageIcon::Clock: pix(kClockIcon, x, 6); x += 14; break;
    case UsageIcon::Face: face(v.headline_face, x, 5); x += 18; break;
    case UsageIcon::None: break;
  }
  text(v.headline.c_str(), x, 18, 9);
  x += text_w(v.headline.c_str(), 9) + 5;
  if (v.pick >= 0 && v.pick < 4) {
    bitmap(*kPickLogos[v.pick], x, 4);
    text(kUsageProviderNames[v.pick], x + 19, 18, 9, true);
  }
  rect(12, 24, 376, 1);
}

void footer(const UsageView& v, const BatteryReading& batt, RuntimeMode mode) {
  rect(12, 275, 376, 1);
  constexpr int16_t base = 294;
  int16_t x = 388;
  x -= chip(runtime_mode_name(mode), x, base, Align::Right) + 8;
  char pct[8];
  if (batt.ok) std::snprintf(pct, sizeof(pct), "%u%%", (unsigned)batt.pct);
  else std::snprintf(pct, sizeof(pct), "--%%");
  text(pct, x, base, 9, false, Align::Right);
  x -= text_w(pct, 9) + 6;
  const int16_t bx = x - 24, by = 283;
  outline(bx, by, 22, 12);
  rect(bx + 22, by + 4, 2, 4);
  if (batt.ok) rect(bx + 2, by + 2, (int16_t)((18 * std::min<unsigned>(batt.pct, 100) + 50) / 100), 8);
  if (!v.footer_chip.empty()) {
    const int16_t w = chip(v.footer_chip.c_str(), 12, base, Align::Left);
    text(v.footer_text.c_str(), 12 + w + 6, base, 9);
  } else {
    text(v.footer_text.c_str(), 12, base, 9);
  }
}

// --- rows --------------------------------------------------------------------
void number(const UsageRowView& r, int16_t right, int16_t baseline, int16_t box_top, float sweep) {
  if (r.state == UsageRowState::Unavailable) {
    text("--", right + 20, baseline, 24, true, Align::Right);
    return;
  }
  char s[8];  // the sweep burns down from a full tank while the car drives in
  std::snprintf(s, sizeof(s), "%d", (int)lroundf(100.0f - (100 - r.left) * sweep));
  const int16_t nw = text_w(s, 24, true), pw = text_w("%", 12, true);
  const bool low = r.left <= 10;
  const uint16_t color = low ? kPaper : kInk;
  if (low) rect(right - nw - 6, box_top, nw + pw + 14, 45);
  text(s, right, baseline, 24, true, Align::Right, color);
  text("%", right + 2, baseline, 12, true, Align::Left, color);
}

void road(const UsageRowView& r, int16_t y, const UsageFrame& f) {
  const int16_t x0 = kRoadX0, x1 = kRoadX1;
  pix(kPump, x1 + 4, y - 6);  // base level with the road's bottom edge
  if (r.state == UsageRowState::Unavailable || r.car_px < 0) {
    for (int16_t x = x0; x < x1; x += 6) rect(x, y + 3, 2, 1);
    return;
  }
  const bool stale = r.state == UsageRowState::Stale;
  const bool moving = f.moving && !stale && r.mood != UsageMood::Nap;
  const int16_t car_at = x0 + (int16_t)lroundf(r.car_px * f.sweep);
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
    const int16_t px = x0 + r.pace_px;
    rect(px - 1, y, 3, 7, kPaper);
    rect(px, y, 1, 8);
    pix(kPaceMark, px - 2, y + 8);
  }
  const int16_t car_x = std::max<int16_t>(x0 - 8, std::min<int16_t>(car_at - 11, x1 - 22));
  const int16_t bob = moving ? (int16_t)(f.tick % 2) : 0;
  car(car_x, y - 13 - bob, stale);
  if (moving) {
    const int k = (int)((f.tick / 2) % 2);
    pix(kRing1, car_x - 5 - k * 2, y - 6 - k);
    if (k) pix(kRing2, car_x - 11, y - 9);
  }
  if (r.mood == UsageMood::Nap) zz(car_x - 17, y - 1, f);
}

void row(size_t i, const UsageRowView& r, const UsageFrame& f) {
  const int16_t top = 26 + (int16_t)i * 62;
  if (i > 0) for (int16_t x = 12; x < 388; x += 3) g->drawPixel(x, top - 1, kInk);
  bitmap(*kRowLogos[i], 12, top + 17);
  number(r, 128, top + 48, top + 9, f.sweep);
  text(kUsageProviderNames[i], kRoadX0, top + 21, 12, true);
  if (i == 3) {  // Gemini is Antigravity's Gemini weekly pool
    bitmap(usage_logos::kAntigravity12, kRoadX0 + text_w(kUsageProviderNames[i], 12, true) + 6, top + 9);
  }
  if (!r.chip.empty()) {
    chip(r.chip.c_str(), 388, top + 21, Align::Right);
  } else if (!r.verdict.empty() && r.state != UsageRowState::Stale) {
    text(r.verdict.c_str(), 388, top + 21, 9, r.runs_dry, Align::Right);
  }
  road(r, top + 38, f);
  text(r.countdown.c_str(), 388, top + 45, 9, false, Align::Right);
  // Clock before the countdown: time until the allowance refills.
  if (r.countdown != "--") pix(kClockIcon, 388 - text_w(r.countdown.c_str(), 9) - 12, top + 35);
}

}  // namespace

void render_usage(const UsageView& view, const BatteryReading& batt,
                  RuntimeMode runtime_mode, const UsageFrame& frame) {
  g = display_canvas();
  if (!g) return;
  g->fillSprite(kPaper);
  headline(view, frame);
  for (size_t i = 0; i < view.rows.size(); i++) row(i, view.rows[i], frame);
  footer(view, batt, runtime_mode);
  display_push();
  g = nullptr;
}

}  // namespace rcpaper

#endif  // ARDUINO
