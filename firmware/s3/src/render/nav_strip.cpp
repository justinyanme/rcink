#include "render/nav_strip.h"

#ifdef ARDUINO
#include <M5Unified.h>
#include "render/palette.h"

namespace rcpaper {

void paint_indicator(M5Canvas& canvas,
                     const std::vector<PageRecord>& pages, uint8_t idx) {
  if (pages.empty()) return;
  const int16_t W = canvas.width();
  const int16_t H = canvas.height();
  const int16_t y_strip = H - BOT_STRIP_H;

  // White background + hairline top rule
  canvas.fillRect(0, y_strip, W, BOT_STRIP_H, kE6White);
  canvas.drawFastHLine(0, y_strip, W, kE6Black);

  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(kE6Black);

  const size_t n = pages.size();
  const uint8_t prev_idx = (uint8_t)((idx + n - 1) % n);
  const uint8_t next_idx = (uint8_t)((idx + 1) % n);

  auto upper = [](const std::string& s) {
    String out = s.c_str();
    out.toUpperCase();
    return out;
  };
  const String prev_id = upper(pages[prev_idx].id);
  const String next_id = upper(pages[next_idx].id);
  const String curr_id = upper(pages[idx].id);

  const int16_t y_text = y_strip + 4;
  const int16_t y_mid  = y_strip + BOT_STRIP_H / 2;

  // Left: ◀ PREV
  const int16_t lpad = 8;
  canvas.fillTriangle(lpad,     y_mid,
                      lpad + 7, y_strip + 5,
                      lpad + 7, y_strip + BOT_STRIP_H - 5, kE6Black);
  canvas.setCursor(lpad + 13, y_text);
  canvas.print(prev_id.c_str());

  // Right: NEXT ▶
  const int16_t rpad = 8;
  const int16_t arrow_x = W - rpad - 7;
  canvas.fillTriangle(arrow_x,     y_strip + 5,
                      arrow_x,     y_strip + BOT_STRIP_H - 5,
                      arrow_x + 7, y_mid, kE6Black);
  const int16_t next_w = canvas.textWidth(next_id.c_str());
  canvas.setCursor(arrow_x - 5 - next_w, y_text);
  canvas.print(next_id.c_str());

  // Center: pagination dots + current page name.
  // Layout: [dots]  [gap]  CURRENT
  const int16_t dot_gap = 10;
  const int16_t kNameGap = 12;   // gap between last dot and page name
  const int16_t dots_w  = (n > 1) ? (int16_t)((n - 1) * dot_gap) : 0;
  const int16_t curr_w  = canvas.textWidth(curr_id.c_str());
  const int16_t group_w = dots_w + (n > 1 ? kNameGap : 0) + curr_w;
  int16_t dx = (W - group_w) / 2;
  // Only draw dots when there's more than one page; a single dot adds no info.
  if (n > 1) {
    for (size_t i = 0; i < n; ++i) {
      if (i == idx) canvas.fillCircle(dx, y_mid, 3, kE6Black);
      else          canvas.drawCircle(dx, y_mid, 2, kE6Black);
      dx += dot_gap;
    }
  }
  // After dots, draw the current page name to the right.
  int16_t name_x = (n > 1) ? (dx - dot_gap + kNameGap) : (W - curr_w) / 2;
  canvas.setCursor(name_x, y_text);
  canvas.print(curr_id.c_str());
}

}  // namespace rcpaper
#endif  // ARDUINO
