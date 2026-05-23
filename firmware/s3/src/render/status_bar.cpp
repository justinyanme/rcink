#include "render/status_bar.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <M5Unified.h>
#include <cmath>
#include <cstring>
#include <time.h>

#include "config.h"
#include "render/icons_data.h"
#include "render/palette.h"
#include "render/warnings.h"

namespace rcpaper {

static String format_local_hhmm(uint64_t epoch_s) {
  if (epoch_s == 0) return String("--:--");
  // Both producer data_ts and firmware last_fetch_s are stored as UTC epoch
  // seconds. LOCAL_TZ is a display rule only, applied identically here.
  // TZ env is set once in setup() and survives within the process.
  time_t t = (time_t)epoch_s;
  struct tm tm {};
  if (!localtime_r(&t, &tm)) return String("--:--");
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
  return String(buf);
}

static void blit_icon16(M5Canvas& canvas, int16_t x, int16_t y, const char* name) {
  using rcpaper::icons::kGlyphs;
  for (int i = 0; kGlyphs[i].name; ++i) {
    if (strcmp(kGlyphs[i].name, name) == 0) {
      const uint8_t* px = kGlyphs[i].glyph.px16;
      for (int yy = 0; yy < 16; ++yy) {
        uint16_t row = ((uint16_t)px[yy * 2] << 8) | px[yy * 2 + 1];
        for (int xx = 0; xx < 16; ++xx) {
          if (row & (0x8000 >> xx)) canvas.drawPixel(x + xx, y + yy, kE6Black);
        }
      }
      return;
    }
  }
  Serial.printf("[blit_icon16] unknown icon: %s\n", name);
}

// One status-bar group = "[icon?] text". Battery is the only group with no
// icon flag here (its glyph is custom-drawn, not bitmap), but its fixed-width
// content is folded into `fixed_pre` below so the measure/pack code stays
// uniform.
struct Group {
  const char* icon;   // nullptr to skip icon blit
  String      text;
};

void paint_status_bar(M5Canvas& canvas,
                      const PageRecord& rec,
                      const BatteryReading& batt,
                      const EnvReading& env,
                      uint64_t last_fetch_s) {
  const int16_t W = canvas.width();
  const int16_t y = 0;
  const int16_t kRightMargin = 6;
  const int16_t kIconW = 18;  // 16px glyph + 2px breathing room

  canvas.fillRect(0, y, W, TOP_STRIP_H, kE6White);
  canvas.drawFastHLine(0, y + TOP_STRIP_H - 1, W, kE6Black);

  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(kE6Black);

  const int16_t y_text = y + 4;
  const int16_t y_icon = y + 3;

  // --- Build the group list. Order matters: leftmost = highest priority. ---
  char num[16];
  Group groups[6];
  // batt%
  if (batt.ok) snprintf(num, sizeof(num), "%d%%", (int)batt.pct);
  else         snprintf(num, sizeof(num), "--");
  groups[0] = {nullptr, String(num)};
  // temp
  if (env.ok) snprintf(num, sizeof(num), "%ld\xC2\xB0""C", lroundf(env.temp_c));
  else        snprintf(num, sizeof(num), "--\xC2\xB0""C");
  groups[1] = {"thermometer", String(num)};
  // humidity
  if (env.ok) snprintf(num, sizeof(num), "%d%%", (int)(env.humidity_pct + 0.5f));
  else        snprintf(num, sizeof(num), "--%%");
  groups[2] = {"droplet", String(num)};
  // data_ts (when the producer built this page)
  groups[3] = {"download", format_local_hhmm(rec.data_ts)};
  // last refresh (when this device last fetched)
  groups[4] = {"refresh", format_local_hhmm(last_fetch_s)};
  // next refresh (scheduled wake)
  uint64_t next_s = last_fetch_s ? last_fetch_s + FETCH_INTERVAL_SEC : 0;
  groups[5] = {"clock", format_local_hhmm(next_s)};
  const int n = (int)(sizeof(groups) / sizeof(groups[0]));

  // --- Measure: fixed pre-content (battery glyph + leading margin) + each
  // group's icon+text. Then pick the largest gap from {8,6,4,2} that fits all
  // groups within the strip; fall back to 2 + warn if even that is too tight.
  const int16_t batt_w = 22;
  int16_t fixed_pre = 6 /*left margin*/ + batt_w + 6 /*post-batt gap*/;
  int16_t content_w = fixed_pre;
  for (int i = 0; i < n; ++i) {
    if (groups[i].icon) content_w += kIconW;
    content_w += canvas.textWidth(groups[i].text.c_str());
  }
  const int n_gaps = n - 1;  // gaps between adjacent groups
  int16_t gap = 2;
  for (int g : {8, 6, 4, 2}) {
    if (content_w + n_gaps * g + kRightMargin <= W) { gap = g; break; }
  }
  if (content_w + n_gaps * 2 + kRightMargin > W) {
    char buf[80];
    snprintf(buf, sizeof(buf),
             "status_bar: doesn't fit (need=%d W=%d) — some groups elided",
             (int)(content_w + n_gaps * 2 + kRightMargin), (int)W);
    render_warn(buf);
  }

  // --- Battery glyph: outline 22x10 with 2x4 nipple, fill 0..18px wide. ---
  int16_t x = 6;
  const int16_t batt_h = 10;
  const int16_t batt_y = y + (TOP_STRIP_H - batt_h) / 2;
  uint32_t fill_c = kE6Green;            // green (>=30%)
  if (batt.pct < 30) fill_c = kE6Yellow; // yellow (10..29%)
  if (batt.pct < 10) fill_c = kE6Red;    // red   (<10%)
  canvas.drawRect(x, batt_y, batt_w, batt_h, kE6Black);
  canvas.fillRect(x + batt_w, batt_y + 3, 2, 4, kE6Black);
  if (batt.ok) {
    int16_t fill_w = (int16_t)((batt_w - 4) * (int)batt.pct / 100);
    if (fill_w > 0) canvas.fillRect(x + 2, batt_y + 2, fill_w, batt_h - 4, fill_c);
  }
  x += batt_w + 6;

  // --- Paint each group, eliding from the right if a group can't fit. ---
  for (int i = 0; i < n; ++i) {
    int16_t need = (groups[i].icon ? kIconW : 0)
                 + canvas.textWidth(groups[i].text.c_str());
    if (x + need + kRightMargin > W) {
      char buf[80];
      snprintf(buf, sizeof(buf),
               "status_bar: elided group #%d at x=%d need=%d W=%d",
               i, (int)x, (int)need, (int)W);
      render_warn(buf);
      return;
    }
    if (groups[i].icon) { blit_icon16(canvas, x, y_icon, groups[i].icon); x += kIconW; }
    canvas.setCursor(x, y_text); canvas.print(groups[i].text.c_str());
    x += canvas.textWidth(groups[i].text.c_str());
    if (i + 1 < n) x += gap;
  }
}

}  // namespace rcpaper
#endif  // ARDUINO
