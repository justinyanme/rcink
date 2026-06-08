#include "render/page.h"

#include <algorithm>

#ifdef ARDUINO
#include <Arduino.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>

#include "config.h"
#include "display/rm67162.h"
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

String format_local_hhmm(uint64_t epoch_s) {
  if (epoch_s == 0) return String("--:--");
  time_t t = (time_t)epoch_s;
  struct tm tm {};
  if (!localtime_r(&t, &tm)) return String("--:--");
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
  return String(buf);
}

String format_local_wday(uint64_t epoch_s) {
  static const char* kW[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  if (epoch_s == 0) return String("");
  time_t t = (time_t)epoch_s;
  struct tm tm {};
  if (!localtime_r(&t, &tm)) return String("");
  return String(kW[tm.tm_wday]);
}

String format_local_md(uint64_t epoch_s) {
  static const char* kM[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                             "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  if (epoch_s == 0) return String("--");
  time_t t = (time_t)epoch_s;
  struct tm tm {};
  if (!localtime_r(&t, &tm)) return String("--");
  char buf[12];
  snprintf(buf, sizeof(buf), "%s %d", kM[tm.tm_mon], tm.tm_mday);
  return String(buf);
}

// Dark AMOLED theme: the layout is composed light (black ink on white, like the
// e-ink targets), then we swap pure white<->black at push time. Background
// becomes true black (AMOLED pixels off = power saving), body text becomes
// white, and every accent (red/green/blue/yellow) stays saturated — glowing on
// black instead of the flat papercolor look. Fonts are 1-bit so there are no
// grey anti-alias halos to smear. Set false for the white e-ink look.
static constexpr bool kDarkTheme = true;

void push_canvas() {
  if (!g_canvas_ready) return;
  uint16_t* buf = (uint16_t*)g_canvas.getPointer();
  if (kDarkTheme) {
    const size_t n = (size_t)SCREEN_W * SCREEN_H;
    for (size_t i = 0; i < n; ++i) {
      if (buf[i] == 0xFFFF) buf[i] = 0x0000;
      else if (buf[i] == 0x0000) buf[i] = 0xFFFF;
    }
  }
  lcd_PushColors(0, 0, SCREEN_W, SCREEN_H, buf);
}

void draw_battery(int16_t x, int16_t y, const BatteryReading& batt) {
  constexpr int16_t w = 28;
  constexpr int16_t h = 12;
  g_canvas.drawRect(x, y, w, h, c565(Color::Black));
  g_canvas.fillRect(x + w, y + 4, 2, 4, c565(Color::Black));
  if (!batt.ok) return;
  Color fill = Color::Green;
  if (batt.pct < 30) fill = Color::Yellow;
  if (batt.pct < 10) fill = Color::Red;
  const int16_t fill_w = (int16_t)((w - 4) * batt.pct / 100);
  if (fill_w > 0) g_canvas.fillRect(x + 2, y + 2, fill_w, h - 4, c565(fill));
}

// Right-edge chrome strip, next to the two physical buttons. Top block =
// status (battery, clock, page index); bottom block = prev/next hints aligned
// to the buttons. STALE shows as a red chip when the cache is past its window.
void paint_sidebar(const DeviceState& state, const BatteryReading& batt,
                   uint64_t last_fetch_s, bool stale) {
  const int16_t x0 = SCREEN_W - SIDEBAR_W;  // 480
  const int16_t cx = x0 + 6;                // text inset inside the sidebar
  g_canvas.fillRect(x0, 0, SIDEBAR_W, SCREEN_H, rgb565(kRgbWhite));
  g_canvas.drawFastVLine(x0, 0, SCREEN_H, c565(Color::Black));

  g_canvas.setTextFont(1);
  g_canvas.setTextSize(1);
  g_canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));

  // --- status block (top): battery, weekday + date, clock, page index ---
  draw_battery(cx, 8, batt);
  char pct[8];
  if (batt.ok) snprintf(pct, sizeof(pct), "%u%%", (unsigned)batt.pct);
  else snprintf(pct, sizeof(pct), "--");
  g_canvas.setCursor(cx, 24);
  g_canvas.print(pct);

  g_canvas.setCursor(cx, 38);
  g_canvas.print(format_local_wday(last_fetch_s));
  g_canvas.setCursor(cx, 50);
  g_canvas.print(format_local_md(last_fetch_s));
  g_canvas.setCursor(cx, 62);
  g_canvas.print(format_local_hhmm(last_fetch_s));

  char idx[16];
  snprintf(idx, sizeof(idx), "%u/%u",
           (unsigned)(state.pages.empty() ? 0 : state.page_index + 1),
           (unsigned)state.pages.size());
  g_canvas.setTextSize(2);
  g_canvas.setCursor(cx, 78);
  g_canvas.print(idx);
  g_canvas.setTextSize(1);

  if (stale) {
    g_canvas.fillRect(cx, 102, 42, 11, c565(Color::Red));
    g_canvas.setTextColor(rgb565(kRgbWhite), c565(Color::Red));
    g_canvas.setCursor(cx + 4, 104);
    g_canvas.print("STALE");
    g_canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));
  }

  // --- nav hints (bottom, by the buttons) ---
  // Upper button (GPIO21) = prev, lower (GPIO0) = next, so PREV sits on top.
  const int16_t prev_y = SCREEN_H - 58;
  const int16_t next_y = SCREEN_H - 26;
  g_canvas.fillTriangle(cx + 4, prev_y, cx, prev_y + 7, cx + 8, prev_y + 7, c565(Color::Black));
  g_canvas.setCursor(cx + 13, prev_y + 1);
  g_canvas.print("PREV");
  g_canvas.fillTriangle(cx, next_y, cx + 8, next_y, cx + 4, next_y + 7, c565(Color::Black));
  g_canvas.setCursor(cx + 13, next_y);
  g_canvas.print("NEXT");
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

}  // namespace

void display_init() {
  // Official non-touch examples use GPIO38 as onboard LED; touch variants use
  // it as panel power enable. Driving it high is correct for both bring-ups.
  constexpr int kPanelEnablePin = 38;
  pinMode(kPanelEnablePin, OUTPUT);
  digitalWrite(kPanelEnablePin, HIGH);

  Serial.println("[display] rm67162 init");
  rm67162_init();
  lcd_setRotation(1);  // landscape 536x240 (native bar orientation)

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
  lcd_sleep();
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
  constexpr float kSpacingCompact = 0.55f;
  constexpr float kMinScale = 0.45f;
  float scale = 1.0f;
  set_text_scale(scale);
  auto laid = run_layout(root, vp, m, scale * kSpacingCompact);
  int16_t used = (int16_t)(content_bottom(laid) - vp.y);
  for (int pass = 0; pass < 2 && used > SCREEN_H && scale > kMinScale; ++pass) {
    scale *= (float)SCREEN_H / (float)used * 0.97f;
    if (scale < kMinScale) scale = kMinScale;
    set_text_scale(scale);
    laid = run_layout(root, vp, m, scale * kSpacingCompact);
    used = (int16_t)(content_bottom(laid) - vp.y);
  }

  g_canvas.fillSprite(rgb565(kRgbWhite));
  paint_layout(g_canvas, laid);
  paint_sidebar(state, batt, state.last_fetch_s, stale);
  set_text_scale(1.0f);  // chrome renders at nominal size

  push_canvas();
  Serial.printf("[render] page=%s scale=%.2f fit=%d/%d in %u ms\n",
                rec.id.c_str(), scale, (int)used, (int)SCREEN_H,
                (unsigned)(millis() - t0));
}

void render_error(const char* msg) {
  if (!g_canvas_ready) return;
  g_canvas.fillSprite(rgb565(kRgbWhite));
  g_canvas.setTextFont(2);
  g_canvas.setTextSize(1);
  g_canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));
  g_canvas.setCursor(14, 28);
  g_canvas.print("rcink amoled");
  g_canvas.setTextFont(1);
  g_canvas.setCursor(14, 54);
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

#endif  // ARDUINO

}  // namespace rcpaper
