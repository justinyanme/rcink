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

void paint_status_bar(M5Canvas& canvas,
                      const PageRecord& rec,
                      const BatteryReading& batt,
                      const EnvReading& env,
                      uint64_t last_fetch_s) {
  const int16_t W = canvas.width();
  const int16_t y = 0;
  const int16_t kRightMargin = 6;

  canvas.fillRect(0, y, W, TOP_STRIP_H, kE6White);
  canvas.drawFastHLine(0, y + TOP_STRIP_H - 1, W, kE6Black);

  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(kE6Black);

  int16_t x = 6;
  const int16_t y_text = y + 4;
  const int16_t y_icon = y + 3;

  auto fits = [&](int16_t need) {
    if (x + need + kRightMargin <= W) return true;
    Serial.printf("[status] overflow at x=%d need=%d W=%d\n", x, need, W);
    return false;
  };

  // --- Battery glyph: outline 22x10 with 2x4 nipple, fill 0..18px wide. ---
  const int16_t batt_w = 22, batt_h = 10;
  const int16_t batt_y = y + (TOP_STRIP_H - batt_h) / 2;
  uint32_t fill_c = kE6Green;          // green (>=30%)
  if (batt.pct < 30) fill_c = kE6Yellow; // yellow (10..29%)
  if (batt.pct < 10) fill_c = kE6Red;    // red   (<10%)
  canvas.drawRect(x, batt_y, batt_w, batt_h, kE6Black);
  canvas.fillRect(x + batt_w, batt_y + 3, 2, 4, kE6Black);
  if (batt.ok) {
    int16_t fill_w = (int16_t)((batt_w - 4) * (int)batt.pct / 100);
    if (fill_w > 0) canvas.fillRect(x + 2, batt_y + 2, fill_w, batt_h - 4, fill_c);
  }
  x += batt_w + 6;

  char num[16];
  if (batt.ok) snprintf(num, sizeof(num), "%d%%", (int)batt.pct);
  else         snprintf(num, sizeof(num), "--");
  canvas.setCursor(x, y_text);
  canvas.print(num);
  x += canvas.textWidth(num) + 8;

  // --- Temperature ---
  if (env.ok) snprintf(num, sizeof(num), "%ld\xC2\xB0""C", lroundf(env.temp_c));
  else        snprintf(num, sizeof(num), "--\xC2\xB0""C");
  int16_t temp_w = 18 + canvas.textWidth(num) + 8;
  if (!fits(temp_w)) return;
  blit_icon16(canvas, x, y_icon, "thermometer"); x += 18;
  canvas.setCursor(x, y_text); canvas.print(num);
  x += canvas.textWidth(num) + 8;

  // --- Humidity ---
  if (env.ok) snprintf(num, sizeof(num), "%d%%", (int)(env.humidity_pct + 0.5f));
  else        snprintf(num, sizeof(num), "--%%");
  int16_t humid_w = 18 + canvas.textWidth(num) + 8;
  if (!fits(humid_w)) return;
  blit_icon16(canvas, x, y_icon, "droplet"); x += 18;
  canvas.setCursor(x, y_text); canvas.print(num);
  x += canvas.textWidth(num) + 8;

  // --- Data time (page's data_ts) ---
  String s = format_local_hhmm(rec.data_ts);
  int16_t data_w = 18 + canvas.textWidth(s.c_str()) + 8;
  if (!fits(data_w)) return;
  blit_icon16(canvas, x, y_icon, "download"); x += 18;
  canvas.setCursor(x, y_text); canvas.print(s.c_str());
  x += canvas.textWidth(s.c_str()) + 8;

  // --- Last refresh time (device last_fetch_s) ---
  s = format_local_hhmm(last_fetch_s);
  int16_t refresh_w = 18 + canvas.textWidth(s.c_str()) + 8;
  if (!fits(refresh_w)) return;
  blit_icon16(canvas, x, y_icon, "refresh"); x += 18;
  canvas.setCursor(x, y_text); canvas.print(s.c_str());
  x += canvas.textWidth(s.c_str()) + 8;

  // --- Next refresh time (last_fetch_s + FETCH_INTERVAL_SEC) ---
  uint64_t next_s = last_fetch_s ? last_fetch_s + FETCH_INTERVAL_SEC : 0;
  s = format_local_hhmm(next_s);
  int16_t next_w = 18 + canvas.textWidth(s.c_str());
  if (!fits(next_w)) return;
  blit_icon16(canvas, x, y_icon, "clock"); x += 18;
  canvas.setCursor(x, y_text); canvas.print(s.c_str());
}

}  // namespace rcpaper
#endif  // ARDUINO
