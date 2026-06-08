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

void push_canvas() {
  if (!g_canvas_ready) return;
  lcd_PushColors(0, 0, SCREEN_W, SCREEN_H, (uint16_t*)g_canvas.getPointer());
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

  // --- status block (top) ---
  draw_battery(cx, 8, batt);
  char pct[8];
  if (batt.ok) snprintf(pct, sizeof(pct), "%u%%", (unsigned)batt.pct);
  else snprintf(pct, sizeof(pct), "--");
  g_canvas.setCursor(cx, 24);
  g_canvas.print(pct);

  g_canvas.setCursor(cx, 38);
  g_canvas.print(format_local_hhmm(last_fetch_s));

  char idx[16];
  snprintf(idx, sizeof(idx), "%u/%u",
           (unsigned)(state.pages.empty() ? 0 : state.page_index + 1),
           (unsigned)state.pages.size());
  g_canvas.setTextSize(2);
  g_canvas.setCursor(cx, 54);
  g_canvas.print(idx);
  g_canvas.setTextSize(1);

  if (stale) {
    g_canvas.fillRect(cx, 78, 42, 11, c565(Color::Red));
    g_canvas.setTextColor(rgb565(kRgbWhite), c565(Color::Red));
    g_canvas.setCursor(cx + 4, 80);
    g_canvas.print("STALE");
    g_canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));
  }

  // --- nav hints (bottom, by the buttons) ---
  // PREV = upper button (▲), NEXT = lower button (▼). If the physical order is
  // reversed on this unit, swap these two y-offsets.
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

  Size text_size(const TextNode& t) const override {
    const int16_t w = measured_width(canvas_, t.size, t.weight, t.value) + 2 * t.pad;
    int16_t gh = font_height(t.size, t.weight);
    // CJK glyphs are 16px tall; widen short rows (e.g. Sm=8) so the baked font
    // is not clipped by the row above/below.
    if (string_needs_cjk(t.value) && gh < kCjkLineH) gh = kCjkLineH;
    return Size{w, (int16_t)(gh + 2 * t.pad)};
  }

  Size icon_size(const IconNode& i) const override {
    int16_t s = i.size > 0 ? i.size : 16;
    return Size{s, s};
  }

  Size image_size(const ImageNode& i) const override {
    const int16_t w = i.width > 0 ? i.width : 120;
    const int16_t h = i.height > 0 ? i.height : 80;
    return Size{w, h};
  }

 private:
  TFT_eSprite& canvas_;
};

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

bool parse_page_layout(const PageRecord& rec, LayoutNode& root) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, rec.layout_json);
  if (err) {
    Serial.printf("[render] inner parse failed: %s\n", err.c_str());
    return false;
  }
  root = parse_layout(doc.as<JsonVariantConst>());
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
  auto laid = run_layout(root, vp, m);

  g_canvas.fillSprite(rgb565(kRgbWhite));
  paint_layout(g_canvas, laid);
  paint_sidebar(state, batt, state.last_fetch_s, stale);

  push_canvas();
  Serial.printf("[render] page=%s pushed in %u ms\n",
                rec.id.c_str(), (unsigned)(millis() - t0));
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

#endif  // ARDUINO

}  // namespace rcpaper
