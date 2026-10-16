#include "gallery.h"

#include <cstdio>
#include <cstring>

#include "frame.h"
#include "image.h"

namespace rclab::gallery {
namespace {

constexpr unsigned kW = kPanelWidth, kH = kPanelHeight;
constexpr unsigned kHeader = 44;  // title band
constexpr unsigned kFooter = 52;  // two 13 pt lines
constexpr unsigned kMargin = 12;

void rect(uint8_t* p, unsigned x, unsigned y, unsigned w, unsigned h, uint8_t v) {
  for (unsigned row = y; row < y + h && row < kH; ++row) {
    for (unsigned col = x; col < x + w && col < kW; ++col) p[size_t(row) * kW + col] = v;
  }
}

// Baseline `y`; VT323 cap heights: 13 pt 13 px, 16 pt 17, 18 pt 19, 24 pt 26.
unsigned text(uint8_t* p, FontSize size, unsigned x, unsigned y, const char* s, uint8_t v, unsigned scale = 1) {
  return draw_text(p, size, x, y, s, v, scale);
}

// Black title band with page number; `ink`/`paper` are the drive's black/white values.
void header(uint8_t* p, unsigned page, const char* title, uint8_t ink = InkBlack, uint8_t paper = InkWhite) {
  rect(p, 0, 0, kW, kHeader, ink);
  text(p, FontSize::Pt24, kMargin, 34, title, paper);
  char n[8];
  snprintf(n, sizeof(n), "%u/%u", page + 1, kPageCount);
  text(p, FontSize::Pt16, kW - kMargin - 13 * unsigned(strlen(n)), 30, n, paper);
}

// Rule and two caption lines at the bottom: what the drive is, what it costs.
void footer(uint8_t* p, const char* line1, const char* line2, uint8_t ink = InkBlack) {
  rect(p, kMargin, kH - kFooter, kW - 2 * kMargin, 2, ink);
  text(p, FontSize::Pt13, kMargin, kH - kFooter + 22, line1, ink);
  text(p, FontSize::Pt13, kMargin, kH - kFooter + 44, line2, ink);
}

void page_index(uint8_t* p, unsigned) {
  render_fill(p, InkWhite);
  header(p, 0, "rcink PaperColor");
  static constexpr const char* kLines[] = {
      "Own waveforms, no TICKEY LUT.",
      "",
      "1 Index          clean mono",
      "2 Fast mono      1.2 s",
      "3 Clean mono     3.2 s",
      "4 Six colours    4.4 s",
      "5 Colour UI      4.4 s",
      "6 48 colours     2.4 s",
      "7 Native UI     14.4 s",
      "",
      "A  next page",
      "B  previous page",
      "C  page action",
      "C hold  factory clear",
  };
  unsigned y = kHeader + 40;
  for (const char* line : kLines) {
    text(p, FontSize::Pt18, kMargin, y, line, InkBlack);
    y += 30;
  }
  footer(p, "Each page shows its drive + BUSY.", "Measured on this bench 2026-10-16.");
}

void page_fast(uint8_t* p, unsigned step) {
  render_fill(p, InkWhite);
  header(p, 1, "Fast mono");
  text(p, FontSize::Pt16, kMargin, kHeader + 34, "C: count +1 (fast mono)", InkBlack);
  text(p, FontSize::Pt16, kMargin, kHeader + 60, "Only changed pixels drive.", InkBlack);
  char n[8];
  snprintf(n, sizeof(n), "%03u", step % 1000);
  text(p, FontSize::Pt24, 52, 360, n, InkBlack, 5);  // 3 x 95 px wide, 130 px tall
  rect(p, kMargin, 400, (kW - 2 * kMargin) * (step % 10 + 1) / 10, 24, InkBlack);
  footer(p, "Open: clean mono. C: fast, 1.2 s", "Idle pixels may keep a faint ghost.");
}

void page_clean(uint8_t* p, unsigned step) {
  render_fill(p, InkWhite);
  header(p, 2, "Clean mono");
  unsigned y = kHeader + 30;
  text(p, FontSize::Pt13, kMargin, y, "13pt The quick brown fox jumps 0123", InkBlack);
  text(p, FontSize::Pt16, kMargin, y += 28, "16pt The quick brown fox 0123", InkBlack);
  text(p, FontSize::Pt18, kMargin, y += 30, "18pt The quick brown fox", InkBlack);
  text(p, FontSize::Pt24, kMargin, y += 38, "24pt Quick brown fox", InkBlack);
  y += 20;
  for (unsigned s = 0; s < 4; ++s) {  // checkers of 2, 4, 8, 16 px
    const unsigned size = 2u << s, x0 = kMargin + s * 94;
    for (unsigned yy = 0; yy < 90; ++yy)
      for (unsigned xx = 0; xx < 88; ++xx) p[size_t(y + yy) * kW + x0 + xx] = ((xx / size + yy / size) & 1) ? InkWhite : InkBlack;
  }
  y += 110;
  rect(p, 0, y, kW, 50, InkBlack);
  text(p, FontSize::Pt18, kMargin, y + 33, "White on black, 18pt", InkWhite);
  if (step & 1) invert(p, kPixels);
  footer(p, "Every pixel 2 cycles, BUSY 3.2 s.", "C: invert. No ghost, no drift.");
}

// The six inks with their names; `step` rotates them so C changes every block's colour.
void page_six(uint8_t* p, unsigned step) {
  static constexpr struct { Ink ink; const char* name; } kInks[] = {
      {InkBlack, "black"}, {InkWhite, "white"}, {InkYellow, "yellow"},
      {InkRed, "red"},     {InkBlue, "blue"},   {InkGreen, "green"},
  };
  render_fill(p, InkWhite);
  header(p, 3, "Six colours");
  constexpr unsigned kTop = kHeader + 8, kBw = 184, kBh = 150;
  for (unsigned i = 0; i < 6; ++i) {
    const auto& c = kInks[(i + step) % 6];
    const unsigned x = kMargin + (i % 2) * (kBw + 8), y = kTop + (i / 2) * (kBh + 8);
    rect(p, x, y, kBw, kBh, c.ink);
    if (c.ink == InkWhite) {  // outline so the white block shows
      rect(p, x, y, kBw, 2, InkBlack), rect(p, x, y + kBh - 2, kBw, 2, InkBlack);
      rect(p, x, y, 2, kBh, InkBlack), rect(p, x + kBw - 2, y, 2, kBh, InkBlack);
    }
    const uint8_t label = c.ink == InkBlack || c.ink == InkBlue ? InkWhite : InkBlack;
    text(p, FontSize::Pt24, x + 10, y + kBh - 14, c.name, label);
  }
  footer(p, "Own colour, 1 pass, BUSY 4.4 s.", "Native: 14.4 s. C: rotate colours.");
}

// A small dashboard: coloured headings, status bullets, a bar chart. `step` changes the bars.
void dashboard(uint8_t* p, unsigned page, const char* title, unsigned step) {
  render_fill(p, InkWhite);
  header(p, page, title);
  unsigned y = kHeader + 34;
  text(p, FontSize::Pt24, kMargin, y, "Fri 16 Oct", InkRed);
  rect(p, 300, kHeader + 10, 40, 40, InkYellow);  // sun
  text(p, FontSize::Pt18, 250, y, "21C", InkBlue);
  y += 36;
  static constexpr struct { Ink ink; const char* line; } kTasks[] = {
      {InkGreen, "Flash lab firmware"},
      {InkGreen, "Six-colour waveform"},
      {InkRed, "Ghost under white"},
      {InkBlue, "48-colour grid"},
      {InkYellow, "Write the README"},
  };
  for (const auto& t : kTasks) {
    rect(p, kMargin, y - 16, 16, 16, t.ink);
    if (t.ink == InkYellow) rect(p, kMargin, y - 16, 16, 2, InkBlack), rect(p, kMargin, y - 2, 16, 2, InkBlack);
    text(p, FontSize::Pt18, kMargin + 28, y, t.line, InkBlack);
    y += 30;
  }
  y += 10;
  text(p, FontSize::Pt16, kMargin, y, "Updates per day", InkBlack);
  y += 12;
  static constexpr Ink kBars[] = {InkRed, InkYellow, InkGreen, InkBlue, InkBlack};
  const unsigned base = kH - kFooter - 16;
  for (unsigned i = 0; i < 5; ++i) {
    const unsigned h = 30 + ((i * 37 + step * 53) % 120);
    rect(p, kMargin + 10 + i * 74, base - h, 50, h, kBars[i]);
  }
  rect(p, kMargin, base, kW - 2 * kMargin, 2, InkBlack);
}

void page_ui(uint8_t* p, unsigned step) {
  dashboard(p, 4, "Colour UI", step);
  footer(p, "Own colour, 1 pass, BUSY 4.4 s.", "C: new bars, old ones fully gone.");
}

void page_grid(uint8_t* p, unsigned) {
  constexpr unsigned kTop = 40, kPw = 50, kPh = (kH - kTop) / 8;  // 70 px patches
  for (unsigned y = kTop; y < kH; ++y)
    for (unsigned x = 0; x < kW; ++x) p[size_t(y) * kW + x] = uint8_t(((y - kTop) / kPh) * 8 + x / kPw);
  rect(p, 0, 0, kW, kTop, GridWhite);
  text(p, FontSize::Pt18, kMargin, 28, "48 colours (8 x 8)", GridBlack);
  char n[8];
  snprintf(n, sizeof(n), "%u/%u", 6u, kPageCount);
  text(p, FontSize::Pt16, kW - kMargin - 13 * unsigned(strlen(n)), 28, n, GridBlack);
}

void page_native(uint8_t* p, unsigned step) {
  dashboard(p, 6, "Native refresh", step);
  footer(p, "Factory refresh: BUSY 14.4 s.", "Same page as 5/7, to compare.");
}

}  // namespace

const Page kPages[] = {
    {"index", Drive::CleanMono, Drive::CleanMono, page_index},
    {"fast", Drive::CleanMono, Drive::FastMono, page_fast},
    {"clean", Drive::CleanMono, Drive::CleanMono, page_clean},
    {"six", Drive::Color, Drive::Color, page_six},
    {"ui", Drive::Color, Drive::Color, page_ui},
    {"grid48", Drive::Grid, Drive::Grid, page_grid},
    {"native", Drive::Native, Drive::Native, page_native},
};
static_assert(sizeof(kPages) / sizeof(kPages[0]) == kPageCount, "kPageCount");

const char* drive_name(Drive drive) {
  switch (drive) {
    case Drive::CleanMono: return "clean-mono";
    case Drive::FastMono: return "fast-mono";
    case Drive::Color: return "own-colour";
    case Drive::Grid: return "own-grid";
    case Drive::Native: return "native";
  }
  return "?";
}

}  // namespace rclab::gallery
