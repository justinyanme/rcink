#include "render/page.h"

#include <algorithm>

#ifdef ARDUINO
#include <Arduino.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>

#include "config.h"
#include "display/epd397.h"
#include "proto/parser.h"
#include "render/engine.h"
#include "render/fonts.h"
#include "render/palette.h"
#include "render/primitives.h"
#endif

namespace rcpaper {

bool layout_has_remote_image(const LayoutNode& node) {
  if (node.type == NodeType::Image && !node.as_image.url.empty()) return true;
  if (node.type != NodeType::VStack && node.type != NodeType::HStack) return false;
  for (const auto& child : node.as_stack.children) {
    if (layout_has_remote_image(child)) return true;
  }
  return false;
}

bool layout_has_any_image(const LayoutNode& node) {
  if (node.type == NodeType::Image) return true;
  if (node.type != NodeType::VStack && node.type != NodeType::HStack) return false;
  for (const auto& child : node.as_stack.children) {
    if (layout_has_any_image(child)) return true;
  }
  return false;
}

static bool layout_has_unknown_node(const LayoutNode& node) {
  if (node.type == NodeType::Unknown) return true;
  if (node.type != NodeType::VStack && node.type != NodeType::HStack) return false;
  for (const auto& child : node.as_stack.children) {
    if (layout_has_unknown_node(child)) return true;
  }
  return false;
}

#ifdef ARDUINO

namespace {
TFT_eSPI g_tft;
TFT_eSprite g_canvas(&g_tft);
bool g_canvas_ready = false;

uint16_t rgb565(uint32_t rgb) {
  const uint8_t r = (rgb >> 16) & 0xFF;
  const uint8_t g = (rgb >> 8) & 0xFF;
  const uint8_t b = rgb & 0xFF;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t c565(Color c) {
  return color_to_rgb565(c);
}

// An unsynced clock (no RTC time, no NTP) reads as a tiny epoch — a bogus 1970
// date, not 0 — so guard on the year, not just epoch==0, to show a placeholder
// instead of a misleading "08:00".
static bool clock_valid(uint64_t epoch_s, struct tm& out) {
  if (epoch_s == 0) return false;
  time_t t = (time_t)epoch_s;
  if (!localtime_r(&t, &out)) return false;
  return out.tm_year + 1900 >= 2024;
}

String format_local_hhmm(uint64_t epoch_s) {
  struct tm tm {};
  if (!clock_valid(epoch_s, tm)) return String("--:--");
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
  return String(buf);
}

String format_local_wday(uint64_t epoch_s) {
  static const char* kW[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  struct tm tm {};
  if (!clock_valid(epoch_s, tm)) return String("");
  return String(kW[tm.tm_wday]);
}

String format_local_md(uint64_t epoch_s) {
  static const char* kM[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                             "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  struct tm tm {};
  if (!clock_valid(epoch_s, tm)) return String("--");
  char buf[12];
  snprintf(buf, sizeof(buf), "%s %d", kM[tm.tm_mon], tm.tm_mday);
  return String(buf);
}

// The e-paper is read black-on-white, so there is no AMOLED white<->black swap:
// the scene is composed black ink on a white field and the driver quantises
// every sprite pixel to a 4-level gray code by luminance (see render/gray_pack.h),
// so antialiased Latin edges survive as real grays.
void push_canvas() {
  if (!g_canvas_ready) return;
  epd397_push((const uint16_t*)g_canvas.getPointer(), SCREEN_W, SCREEN_H);
}

void draw_battery(int16_t x, int16_t y, const BatteryReading& batt) {
  constexpr int16_t w = 56;
  constexpr int16_t h = 24;
  g_canvas.drawRect(x, y, w, h, c565(Color::Black));
  g_canvas.fillRect(x + w, y + 8, 3, 8, c565(Color::Black));  // terminal nub
  if (!batt.ok) return;
  Color fill = Color::Green;
  if (batt.pct < 30) fill = Color::Yellow;
  if (batt.pct < 10) fill = Color::Red;
  const int16_t fill_w = (int16_t)((w - 6) * batt.pct / 100);
  if (fill_w > 0) g_canvas.fillRect(x + 3, y + 3, fill_w, h - 6, c565(fill));
}

// Page-position rail: a scrollbar "thumb" folded onto the content/sidebar
// divider. The thumb is 1/N of the track tall and slides top->bottom as the
// deck advances, so position-in-deck reads at a glance without spending a
// status-block row on "N/M" text. A fat black thumb straddles the thin black
// divider line. Skipped for single-page decks (nothing to page through).
void draw_page_rail(int16_t divider_x, size_t count, size_t index) {
  if (count <= 1) return;
  constexpr int16_t kTop = 12;
  constexpr int16_t kBot = SCREEN_H - 112;  // stay clear of the 3-line nav block
  const int16_t track = kBot - kTop;
  int16_t th = (int16_t)(track / (int16_t)count);
  if (th < 10) th = 10;
  int16_t ty = (int16_t)(kTop + (int32_t)track * (int32_t)index / (int32_t)count);
  if (ty + th > kBot) ty = (int16_t)(kBot - th);
  g_canvas.fillRect((int16_t)(divider_x - 3), ty, 7, th, c565(Color::Black));
}

// Right-edge chrome strip, next to the two physical buttons. Top block =
// status (battery, clock) with a page-position rail on the divider; bottom
// block = prev/next hints aligned to the buttons. STALE shows as a red chip
// when the cache is past its window.
void paint_sidebar(const DeviceState& state, const BatteryReading& batt,
                   uint64_t last_fetch_s, bool stale) {
  const int16_t x0 = SCREEN_W - SIDEBAR_W;  // 690
  const int16_t cx = x0 + 8;                // text inset inside the sidebar
  g_canvas.fillRect(x0, 0, SIDEBAR_W, SCREEN_H, rgb565(kRgbWhite));
  g_canvas.drawFastVLine(x0, 0, SCREEN_H, c565(Color::Black));

  g_canvas.setTextFont(1);
  g_canvas.setTextSize(2);
  g_canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));

  // --- status block (top): battery, weekday + date, clock; page-position
  //     rail rides the divider (drawn below) instead of a "N/M" text row ---
  draw_battery(cx, 14, batt);
  char pct[8];
  if (batt.ok) snprintf(pct, sizeof(pct), "%u%%", (unsigned)batt.pct);
  else snprintf(pct, sizeof(pct), "--");
  g_canvas.setCursor(cx, 46);
  g_canvas.print(pct);

  g_canvas.setCursor(cx, 80);
  g_canvas.print(format_local_wday(last_fetch_s));
  g_canvas.setCursor(cx, 104);
  g_canvas.print(format_local_md(last_fetch_s));
  g_canvas.setCursor(cx, 128);
  g_canvas.print(format_local_hhmm(last_fetch_s));

  draw_page_rail(x0, state.pages.size(), state.page_index);

  if (stale) {
    g_canvas.fillRect(cx, 162, 84, 22, c565(Color::Red));
    g_canvas.setTextColor(rgb565(kRgbWhite), c565(Color::Red));
    g_canvas.setCursor(cx + 6, 166);
    g_canvas.print("STALE");
    g_canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));
  }

  // --- nav hints (bottom): the 3-way wheel maps UP=prev, FN=home, DOWN=next ---
  g_canvas.setCursor(cx, SCREEN_H - 96);
  g_canvas.print("UP PREV");
  g_canvas.setCursor(cx, SCREEN_H - 64);
  g_canvas.print("FN HOME");
  g_canvas.setCursor(cx, SCREEN_H - 32);
  g_canvas.print("DN NEXT");
}

class SpriteMeasurer : public Measurer {
 public:
  explicit SpriteMeasurer(TFT_eSprite& canvas) : canvas_(canvas) {}

  // All extents honour the active fit-to-height scale (set_text_scale): text
  // shrinks down the font ladder, padding/icons/images scale continuously.
  Size text_size(const TextNode& t) const override {
    const int16_t pad = scale_px(t.pad);
    const int16_t w = measured_width(canvas_, t.size, t.weight, t.value) + 2 * pad;
    int16_t gh = text_line_height(t.size);
    // CJK glyphs have a 16px floor; widen short rows so they are not clipped.
    if (string_needs_cjk(t.value) && gh < kCjkLineH) gh = kCjkLineH;
    return Size{w, (int16_t)(gh + 2 * pad)};
  }

  Size icon_size(const IconNode& i) const override {
    const int16_t s = scale_px(i.size > 0 ? i.size : 16);
    return Size{s, s};
  }

  Size image_size(const ImageNode& i) const override {
    const int16_t w = scale_px(i.width > 0 ? i.width : 120);
    const int16_t h = scale_px(i.height > 0 ? i.height : 80);
    return Size{w, h};
  }

 private:
  TFT_eSprite& canvas_;
};

// Deepest pixel a laid-out tree reaches (max bottom edge), for fit-to-height.
int16_t content_bottom(const LaidOutNode& n) {
  int16_t b = (int16_t)(n.rect.y + n.rect.h);
  for (const auto& c : n.children) b = std::max(b, content_bottom(c));
  return b;
}

// Right-most pixel a laid-out tree reaches, for fit-to-width: a wide masthead
// (e.g. the overview's long quote) scales the whole page down rather than
// running under the sidebar.
int16_t content_right(const LaidOutNode& n) {
  int16_t r = (int16_t)(n.rect.x + n.rect.w);
  for (const auto& c : n.children) r = std::max(r, content_right(c));
  return r;
}

}  // namespace

void display_init() {
  Serial.println("[display] epd397 init");
  epd397_init();  // landscape 800x480, 4-level grayscale e-paper

  g_canvas.setColorDepth(16);
  g_canvas.setSwapBytes(true);
  g_canvas_ready = g_canvas.createSprite(SCREEN_W, SCREEN_H) != nullptr;
  if (!g_canvas_ready) {
    Serial.println("[display] sprite alloc FAILED");
    return;
  }
  fonts_begin(g_canvas);  // bind the u8g2 CJK renderer to the sprite buffer
  g_canvas.fillSprite(rgb565(kRgbWhite));
  push_canvas();
  Serial.printf("[display] ready %dx%d free_heap=%u psram=%u\n",
                SCREEN_W, SCREEN_H,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());
}

void display_sleep() {
  epd397_sleep();
}

void clear_screen() {
  if (!g_canvas_ready) return;
  g_canvas.fillSprite(rgb565(kRgbWhite));
  push_canvas();
}

// Producer page mastheads emit the date as `now.strftime("%a · %b %d").upper()`
// — e.g. "MON · JUN 08". On the landscape t-amoled the weekday/date already
// live in the right sidebar, so this node is redundant. The shared layout_json
// can't be changed per-device (s3's full status bar relies on this content
// date), so we drop it here, on this target only. Matched by the specific
// weekday-led "AAA · " prefix so it can't collide with body content.
static bool is_masthead_date(const std::string& v) {
  static const char* kW[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
  for (const char* w : kW) {
    const std::string prefix = std::string(w) + " \xC2\xB7 ";  // "AAA · "
    if (v.size() >= prefix.size() && v.compare(0, prefix.size(), prefix) == 0) return true;
  }
  return false;
}

static void prune_masthead_date(LayoutNode& n) {
  if (n.type != NodeType::VStack && n.type != NodeType::HStack) return;
  auto& kids = n.as_stack.children;
  kids.erase(std::remove_if(kids.begin(), kids.end(), [](const LayoutNode& c) {
               return c.type == NodeType::Text && is_masthead_date(c.as_text.value);
             }),
             kids.end());
  for (auto& c : kids) prune_masthead_date(c);
}

bool parse_page_layout(const PageRecord& rec, LayoutNode& root) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, rec.layout_json);
  if (err) {
    Serial.printf("[render] inner parse failed: %s\n", err.c_str());
    return false;
  }
  root = parse_layout(doc.as<JsonVariantConst>());
  prune_masthead_date(root);  // de-dup: sidebar already shows weekday/date
  if (layout_has_unknown_node(root)) {
    Serial.println("[render] layout rejected: unknown node type or invalid color token");
    return false;
  }
  return true;
}

void render_page(const PageRecord& rec, const LayoutNode& root, bool stale,
                 const DeviceState& state, const BatteryReading& batt) {
  if (!g_canvas_ready) {
    Serial.println("[render] skipped: display not ready");
    return;
  }
  const uint32_t t0 = millis();
  SpriteMeasurer m(g_canvas);
  Rect vp{0, 0, CONTENT_W, SCREEN_H};  // full height, left of the right sidebar

  // Spacing compaction: the panel is small but high-res, so we tighten the
  // producer's whitespace (pads/gaps/spacers/bar heights) to ~half. Text is NOT
  // compacted — packing the layout tighter leaves more room for the fit pass to
  // keep fonts large. Text scale (g_text_scale) and the engine's spacing scale
  // are therefore decoupled: spacing = scale * kSpacingCompact, text = scale.
  constexpr float kSpacingCompact = 0.70f;
  constexpr float kMinScale = 0.45f;
  float scale = 1.0f;
  set_text_scale(scale);
  auto laid = run_layout(root, vp, m, scale * kSpacingCompact);
  int16_t used_h = (int16_t)(content_bottom(laid) - vp.y);
  int16_t used_w = (int16_t)(content_right(laid) - vp.x);
  for (int pass = 0;
       pass < 3 && (used_h > SCREEN_H || used_w > CONTENT_W) && scale > kMinScale;
       ++pass) {
    const float fit_h = (float)SCREEN_H / (float)used_h;
    const float fit_w = (float)CONTENT_W / (float)used_w;
    scale *= std::min(fit_h, fit_w) * 0.97f;
    if (scale < kMinScale) scale = kMinScale;
    set_text_scale(scale);
    laid = run_layout(root, vp, m, scale * kSpacingCompact);
    used_h = (int16_t)(content_bottom(laid) - vp.y);
    used_w = (int16_t)(content_right(laid) - vp.x);
  }

  g_canvas.fillSprite(rgb565(kRgbWhite));
  paint_layout(g_canvas, laid);
  paint_sidebar(state, batt, state.last_fetch_s, stale);
  set_text_scale(1.0f);  // chrome renders at nominal size

  push_canvas();
  Serial.printf("[render] page=%s scale=%.2f fit=%dx%d/%dx%d in %u ms\n",
                rec.id.c_str(), scale, (int)used_w, (int)used_h,
                (int)CONTENT_W, (int)SCREEN_H, (unsigned)(millis() - t0));
}

void render_error(const char* msg) {
  if (!g_canvas_ready) return;
  g_canvas.fillSprite(rgb565(kRgbWhite));
  g_canvas.setTextFont(2);
  g_canvas.setTextSize(2);
  g_canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));
  g_canvas.setCursor(24, 40);
  g_canvas.print("rcink epd397");
  g_canvas.setTextFont(2);
  g_canvas.setTextSize(1);
  g_canvas.setCursor(24, 100);
  g_canvas.print(printable_text(msg).c_str());
  push_canvas();
}

void render_status(const char* msg) {
  if (!g_canvas_ready) return;
  set_text_scale(1.0f);
  g_canvas.fillSprite(rgb565(kRgbWhite));
  const std::string s = msg;
  const int16_t w = measured_width(g_canvas, TextSize::H2, Weight::Bold, s);
  const int16_t lh = text_line_height(TextSize::H2);
  int16_t x = (int16_t)((CONTENT_W - w) / 2);
  if (x < 6) x = 6;
  draw_utf8(g_canvas, x, (int16_t)((SCREEN_H - lh) / 2), TextSize::H2, Weight::Bold,
            c565(Color::Black), false, rgb565(kRgbWhite), s);
  push_canvas();
}

// --- dashboard (hand-drawn, mirrors tools/preview_dashboard.py) -------------
namespace {

// FreeSans/Bold GFX string, top-left at (x, y_top). The fonts.cpp ladder tops
// out at 24pt; the hero number needs an extra integer setTextSize on top, so
// the dashboard draws directly instead of going through the layout engine.
void gfx_text(int16_t x, int16_t y_top, const char* s, const GFXfont* font,
              uint8_t tsize, uint16_t color) {
  g_canvas.setFreeFont(font);
  g_canvas.setTextSize(tsize);
  g_canvas.setTextWrap(false, false);
  g_canvas.setTextDatum(TL_DATUM);
  g_canvas.setTextColor(color);
  g_canvas.drawString(s, x, y_top);
}

int16_t gfx_w(const char* s, const GFXfont* font, uint8_t tsize) {
  g_canvas.setFreeFont(font);
  g_canvas.setTextSize(tsize);
  return g_canvas.textWidth(s);
}

// Outlined progress bar with a solid black fill (reflective e-ink style).
void dash_bar(int16_t x, int16_t y, int16_t w, int16_t h, float pct) {
  g_canvas.drawRect(x, y, w, h, c565(Color::Black));
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  const int16_t fw = (int16_t)((w - 4) * pct / 100.0f);
  if (fw > 0) g_canvas.fillRect(x + 2, y + 2, fw, h - 4, c565(Color::Black));
}

}  // namespace

void render_dashboard(const Shtc3Reading& env, const BatteryReading& batt,
                      uint64_t epoch) {
  if (!g_canvas_ready) return;
  set_text_scale(1.0f);
  g_canvas.fillSprite(rgb565(kRgbWhite));
  const uint16_t ink = c565(Color::Black);
  constexpr int16_t PAD = 28;

  // --- top status bar ---
  gfx_text(PAD, 14, "RCINK", &FreeSansBold24pt7b, 1, ink);
  gfx_text(PAD, 70, "DESK CLIMATE", &FreeSansBold12pt7b, 1, ink);
  const String clk = format_local_hhmm(epoch);
  const int16_t cw = gfx_w(clk.c_str(), &FreeSansBold24pt7b, 2);
  gfx_text((int16_t)(SCREEN_W - PAD - cw), 8, clk.c_str(), &FreeSansBold24pt7b, 2, ink);
  String dd = format_local_wday(epoch);
  if (dd.length()) dd += "  ";
  dd += format_local_md(epoch);
  const int16_t dw = gfx_w(dd.c_str(), &FreeSans12pt7b, 1);
  gfx_text((int16_t)(SCREEN_W - PAD - dw), 86, dd.c_str(), &FreeSans12pt7b, 1, ink);
  g_canvas.drawFastHLine(PAD, 116, (int16_t)(SCREEN_W - 2 * PAD), ink);

  // --- temperature hero (centered) ---
  const char* tlabel = "TEMPERATURE";
  const int16_t tlw = gfx_w(tlabel, &FreeSans12pt7b, 1);
  gfx_text((int16_t)((SCREEN_W - tlw) / 2), 132, tlabel, &FreeSans12pt7b, 1, ink);
  char num[16];
  if (env.ok) snprintf(num, sizeof(num), "%.1f", env.temp_c);
  else snprintf(num, sizeof(num), "--.-");
  const int16_t nw = gfx_w(num, &FreeSansBold24pt7b, 3);
  constexpr int16_t kDegR = 14;  // degree-ring radius
  const int16_t deg_block = (int16_t)(2 * kDegR + 6 + gfx_w("C", &FreeSansBold24pt7b, 1));
  const int16_t nx = (int16_t)((SCREEN_W - (nw + 10 + deg_block)) / 2);
  const int16_t ny = 168;
  gfx_text(nx, ny, num, &FreeSansBold24pt7b, 3, ink);
  // degree ring + C, top-aligned to the number
  const int16_t dcx = (int16_t)(nx + nw + 10 + kDegR);
  const int16_t dcy = (int16_t)(ny + kDegR + 6);
  g_canvas.fillCircle(dcx, dcy, kDegR, ink);
  g_canvas.fillCircle(dcx, dcy, (int16_t)(kDegR - 5), rgb565(kRgbWhite));
  gfx_text((int16_t)(dcx + kDegR + 6), (int16_t)(ny + 6), "C", &FreeSansBold24pt7b, 1, ink);

  // --- humidity ---
  constexpr int16_t hy = 300;
  gfx_text(PAD, (int16_t)(hy + 10), "HUMIDITY", &FreeSans18pt7b, 1, ink);
  char hum[8];
  if (env.ok) snprintf(hum, sizeof(hum), "%.0f%%", env.rh);
  else snprintf(hum, sizeof(hum), "--%%");
  const int16_t hw = gfx_w(hum, &FreeSansBold24pt7b, 1);
  gfx_text((int16_t)(SCREEN_W - PAD - hw), (int16_t)(hy + 4), hum, &FreeSansBold24pt7b, 1, ink);
  constexpr int16_t hbar_x = PAD + 8;
  dash_bar(hbar_x, (int16_t)(hy + 52), (int16_t)(SCREEN_W - 2 * hbar_x), 22, env.ok ? env.rh : 0.0f);

  // --- bottom battery strip ---
  g_canvas.drawFastHLine(PAD, 410, (int16_t)(SCREEN_W - 2 * PAD), ink);
  constexpr int16_t sy = 424;
  gfx_text(PAD, (int16_t)(sy + 8), "BATTERY", &FreeSans18pt7b, 1, ink);
  char pct[8];
  if (batt.ok) snprintf(pct, sizeof(pct), "%u%%", (unsigned)batt.pct);
  else snprintf(pct, sizeof(pct), "--%%");
  char volt[12];
  snprintf(volt, sizeof(volt), "%.2fV", batt.mv / 1000.0f);
  const int16_t pw = gfx_w(pct, &FreeSansBold18pt7b, 1);
  const int16_t vw = gfx_w(volt, &FreeSans18pt7b, 1);
  gfx_text((int16_t)(SCREEN_W - PAD - pw), (int16_t)(sy + 4), pct, &FreeSansBold18pt7b, 1, ink);
  const int16_t vx = (int16_t)(SCREEN_W - PAD - pw - 20 - vw);
  gfx_text(vx, (int16_t)(sy + 8), volt, &FreeSans18pt7b, 1, ink);
  // Start the bar after the actual "BATTERY" label width (measured, not a magic
  // number) so it never overlaps the label, then run it up to the V/% readout.
  const int16_t blx = (int16_t)(PAD + gfx_w("BATTERY", &FreeSans18pt7b, 1) + 20);
  const int16_t brx = (int16_t)(vx - 20);
  if (brx - blx > 40) {
    dash_bar(blx, (int16_t)(sy + 4), (int16_t)(brx - blx), 28, batt.ok ? (float)batt.pct : 0.0f);
  }

  push_canvas();
}

#endif  // ARDUINO

}  // namespace rcpaper
