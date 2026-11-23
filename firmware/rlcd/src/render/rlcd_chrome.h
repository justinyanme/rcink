// Shared 400x300 1-bit desktop-instrument typography, icons, and chrome
// helpers shared across Allowance, Token, and Monument hand-drawn renderers.
#pragma once

#ifdef ARDUINO

#include <TFT_eSPI.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "power/runtime_policy.h"
#include "render/rlcd_fonts.h"
#include "sense/battery.h"
#include "sense/shtc3.h"

namespace rcpaper::rlcd_chrome {

constexpr uint16_t kInk = 0x0000;
constexpr uint16_t kPaper = 0xFFFF;
constexpr int16_t kMarginX = 12;
constexpr int16_t kRightX = 388;
constexpr int16_t kHeaderBaseline = 19;
constexpr int16_t kHeaderRuleY = 28;
constexpr int16_t kFooterRuleY = 272;
constexpr int16_t kFooterBaseline = 291;
constexpr int16_t kBatteryY = 280;
constexpr int16_t kChipPadding = 4;
constexpr int16_t kChipExtraW = 7;
constexpr int16_t kModeGap = 4;
constexpr int16_t kBatteryGap = 4;
constexpr int16_t kFooterTextGap = 6;
constexpr int16_t kDotGap = 5;

enum class Align : uint8_t { Left, Right, Center };
enum class FontRole : uint8_t { Chip, Body, BodyBold, Name, HeroMd, HeroLg };

inline const GFXfont* font_for(FontRole role) {
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

inline int16_t text_w(TFT_eSprite* g, const char* s, FontRole role) {
  g->setFreeFont(font_for(role));
  g->setTextSize(1);
  return g->textWidth(s);
}

inline void text(TFT_eSprite* g, const char* s, int16_t x, int16_t baseline,
                 FontRole role, Align align = Align::Left,
                 uint16_t color = kInk) {
  g->setFreeFont(font_for(role));
  g->setTextSize(1);
  g->setTextWrap(false, false);
  g->setTextDatum(align == Align::Left   ? L_BASELINE
                  : align == Align::Right ? R_BASELINE
                                          : C_BASELINE);
  g->setTextColor(color);
  g->drawString(s, x, baseline);
}

// Draw UTF-8 middle dots ("\xC2\xB7") as crisp 2x2 ink squares so ASCII-only
// baked fonts never drop separators. Returns total drawn pixel width.
inline int16_t dotted_text_width(TFT_eSprite* g, const std::string& value,
                                 FontRole role = FontRole::Body) {
  const std::string separator = "\xC2\xB7";
  int16_t w = 0;
  size_t pos = 0;
  while (pos <= value.size()) {
    const size_t at = value.find(separator, pos);
    std::string seg = at == std::string::npos ? value.substr(pos)
                                              : value.substr(pos, at - pos);
    while (!seg.empty() && seg.front() == ' ') seg.erase(seg.begin());
    while (!seg.empty() && seg.back() == ' ') seg.pop_back();
    if (!seg.empty()) w += text_w(g, seg.c_str(), role);
    if (at == std::string::npos) break;
    w += 2 * kDotGap + 2;
    pos = at + separator.size();
  }
  return w;
}

inline void draw_dotted_text(TFT_eSprite* g, const std::string& value,
                             int16_t x, int16_t baseline,
                             FontRole role = FontRole::Body,
                             uint16_t color = kInk) {
  const std::string separator = "\xC2\xB7";
  int16_t cur = x;
  size_t pos = 0;
  while (pos <= value.size()) {
    const size_t at = value.find(separator, pos);
    std::string seg = at == std::string::npos ? value.substr(pos)
                                              : value.substr(pos, at - pos);
    while (!seg.empty() && seg.front() == ' ') seg.erase(seg.begin());
    while (!seg.empty() && seg.back() == ' ') seg.pop_back();
    if (!seg.empty()) {
      text(g, seg.c_str(), cur, baseline, role, Align::Left, color);
      cur += text_w(g, seg.c_str(), role);
    }
    if (at == std::string::npos) break;
    cur += kDotGap;
    g->fillRect(cur, baseline - 5, 2, 2, color);
    cur += 2 + kDotGap;
    pos = at + separator.size();
  }
}

template <size_t N>
inline void pixels(TFT_eSprite* g, const char* const (&rows)[N],
                   int16_t x, int16_t y, uint16_t color = kInk) {
  for (size_t j = 0; j < N; j++) {
    for (size_t i = 0; rows[j][i]; i++) {
      if (rows[j][i] == '#') g->drawPixel(x + (int16_t)i, y + (int16_t)j, color);
    }
  }
}

static constexpr const char* kSpark[11] = {
    ".....#.....",
    ".....#.....",
    "....###....",
    "...#####...",
    "..#######..",
    "###########",
    "..#######..",
    "...#####...",
    "....###....",
    ".....#.....",
    ".....#.....",
};

static constexpr const char* kSparkSmall[11] = {
    "...........",
    ".....#.....",
    ".....#.....",
    "....###....",
    "...#####...",
    ".#########.",
    "...#####...",
    "....###....",
    ".....#.....",
    ".....#.....",
    "...........",
};

static constexpr const char* kDegRing4[4] = {
    ".##.",
    "#..#",
    "#..#",
    ".##.",
};

static constexpr const char* kSigma[11] = {
    "########",
    "#......#",
    ".#......",
    "..#.....",
    "...#....",
    "....#...",
    "...#....",
    "..#.....",
    ".#......",
    "#......#",
    "########",
};

inline int16_t chip(TFT_eSprite* g, const char* s, int16_t x, int16_t baseline,
                    Align align = Align::Left) {
  const int16_t w = text_w(g, s, FontRole::Chip) + kChipExtraW;
  const int16_t x0 = align == Align::Right ? x - w
                   : align == Align::Center ? x - w / 2
                                            : x;
  if (w > 0) g->fillRect(x0, baseline - 13, w, 17, kInk);
  text(g, s, x0 + kChipPadding, baseline, FontRole::Chip, Align::Left, kPaper);
  return w;
}

inline const char* runtime_chip_label(RuntimeMode mode, bool carousel_pinned) {
  if (mode == RuntimeMode::Host && carousel_pinned) return "PINNED";
  return runtime_mode_name(mode);
}

inline void draw_footer(TFT_eSprite* g, const std::string& footer_chip,
                        const std::string& footer_text,
                        const BatteryReading& batt, RuntimeMode mode,
                        bool carousel_pinned) {
  g->fillRect(kMarginX, kFooterRuleY, 376, 1, kInk);
  constexpr int16_t base = kFooterBaseline;
  int16_t x = kRightX;
  const char* mode_label = runtime_chip_label(mode, carousel_pinned);
  x -= chip(g, mode_label, x, base, Align::Right) + kModeGap;
  char pct[8];
  if (batt.ok) std::snprintf(pct, sizeof(pct), "%u%%", (unsigned)batt.pct);
  else std::snprintf(pct, sizeof(pct), "--%%");
  text(g, pct, x, base, FontRole::Body, Align::Right);
  x -= text_w(g, pct, FontRole::Body) + kBatteryGap;
  const int16_t bx = x - 24;
  g->drawRect(bx, kBatteryY, 22, 12, kInk);
  g->fillRect(bx + 22, kBatteryY + 4, 2, 4, kInk);
  if (batt.ok) {
    g->fillRect(bx + 2, kBatteryY + 2,
                (int16_t)((18 * std::min<unsigned>(batt.pct, 100) + 50) / 100),
                8, kInk);
  }
  if (!footer_chip.empty()) {
    const int16_t w = chip(g, footer_chip.c_str(), kMarginX, base, Align::Left);
    text(g, footer_text.c_str(), kMarginX + w + kFooterTextGap, base,
         FontRole::Body);
  } else {
    text(g, footer_text.c_str(), kMarginX, base, FontRole::Body);
  }
}

// Measure & draw right-aligned ambient room climate (e.g. "24.2°C · 48%") if
// it clears `min_left` without colliding with left headline text.
inline void draw_ambient_climate_right(TFT_eSprite* g, const Shtc3Reading& env,
                                       int16_t right_x, int16_t baseline,
                                       int16_t min_left, bool show_c = true) {
  if (!env.ok) return;
  char t_buf[12];
  char h_buf[12];
  std::snprintf(t_buf, sizeof(t_buf), "%.1f", env.temp_c);
  std::snprintf(h_buf, sizeof(h_buf), "%.0f%%", env.rh);
  const int16_t tw = text_w(g, t_buf, FontRole::Body);
  const int16_t cw = show_c ? text_w(g, "C", FontRole::Body) + 1 : 0;
  const int16_t hw = text_w(g, h_buf, FontRole::Body);
  const int16_t deg_w = 6;  // 1 px gap + 4 px ring + 1 px gap
  const int16_t dot_w = 2 * kDotGap + 2;
  const int16_t total_w = tw + deg_w + cw + dot_w + hw;
  const int16_t x0 = right_x - total_w;
  if (x0 < min_left) return;

  int16_t cur = x0;
  text(g, t_buf, cur, baseline, FontRole::Body);
  cur += tw + 1;
  pixels(g, kDegRing4, cur, baseline - 12);
  cur += 5;
  if (show_c) {
    text(g, "C", cur, baseline, FontRole::Body);
    cur += text_w(g, "C", FontRole::Body);
  }
  cur += kDotGap;
  g->fillRect(cur, baseline - 5, 2, 2, kInk);
  cur += 2 + kDotGap;
  text(g, h_buf, cur, baseline, FontRole::Body);
}

}  // namespace rcpaper::rlcd_chrome

#endif  // ARDUINO
