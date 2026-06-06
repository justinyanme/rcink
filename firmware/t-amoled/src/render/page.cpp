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

void paint_status_bar(const PageRecord& rec, const BatteryReading& batt, uint64_t last_fetch_s) {
  g_canvas.fillRect(0, 0, SCREEN_W, TOP_STRIP_H, rgb565(kRgbWhite));
  g_canvas.drawFastHLine(0, TOP_STRIP_H - 1, SCREEN_W, c565(Color::Black));

  draw_battery(4, 6, batt);
  g_canvas.setTextFont(1);
  g_canvas.setTextSize(1);
  g_canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));

  char pct[8];
  if (batt.ok) snprintf(pct, sizeof(pct), "%u%%", (unsigned)batt.pct);
  else snprintf(pct, sizeof(pct), "--");
  g_canvas.setCursor(38, 7);
  g_canvas.print(pct);

  const String page = printable_text(rec.id).c_str();
  const int16_t page_w = g_canvas.textWidth(page);
  g_canvas.setCursor((SCREEN_W - page_w) / 2, 7);
  g_canvas.print(page);

  const String stamp = format_local_hhmm(last_fetch_s);
  const int16_t stamp_w = g_canvas.textWidth(stamp);
  g_canvas.setCursor(SCREEN_W - stamp_w - 4, 7);
  g_canvas.print(stamp);
}

void paint_nav_strip(const DeviceState& state) {
  g_canvas.fillRect(0, SCREEN_H - BOT_STRIP_H, SCREEN_W, BOT_STRIP_H, rgb565(kRgbWhite));
  g_canvas.drawFastHLine(0, SCREEN_H - BOT_STRIP_H, SCREEN_W, c565(Color::Black));
  g_canvas.setTextFont(1);
  g_canvas.setTextSize(1);
  g_canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));

  char left[24];
  snprintf(left, sizeof(left), "%u/%u",
           (unsigned)(state.pages.empty() ? 0 : state.page_index + 1),
           (unsigned)state.pages.size());
  g_canvas.setCursor(6, SCREEN_H - BOT_STRIP_H + 10);
  g_canvas.print(left);

  if (!state.pages.empty()) {
    const std::string current = printable_text(state.pages[state.page_index].id);
    const int16_t current_w = g_canvas.textWidth(current.c_str());
    g_canvas.setCursor((SCREEN_W - current_w) / 2, SCREEN_H - BOT_STRIP_H + 10);
    g_canvas.print(current.c_str());
  }

  const char* right = "NEXT";
  const int16_t right_w = g_canvas.textWidth(right);
  g_canvas.setCursor(SCREEN_W - right_w - 6, SCREEN_H - BOT_STRIP_H + 10);
  g_canvas.print(right);
}

class SpriteMeasurer : public Measurer {
 public:
  explicit SpriteMeasurer(TFT_eSprite& canvas) : canvas_(canvas) {}

  Size text_size(const TextNode& t) const override {
    TextNode copy = t;
    const std::string value = printable_text(t.value);
    const int16_t w = text_width(canvas_, copy, value.c_str());
    const int16_t h = font_height(t.size, t.weight) + 2 * t.pad;
    return Size{w, h};
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
  lcd_setRotation(0);

  g_canvas.setColorDepth(16);
  g_canvas.setSwapBytes(true);
  g_canvas_ready = g_canvas.createSprite(SCREEN_W, SCREEN_H) != nullptr;
  if (!g_canvas_ready) {
    Serial.println("[display] sprite alloc FAILED");
    return;
  }
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
  Rect vp{0, TOP_STRIP_H, SCREEN_W, (int16_t)(SCREEN_H - TOP_STRIP_H - BOT_STRIP_H)};
  auto laid = run_layout(root, vp, m);

  g_canvas.fillSprite(rgb565(kRgbWhite));
  paint_layout(g_canvas, laid);
  paint_status_bar(rec, batt, state.last_fetch_s);
  paint_nav_strip(state);

  if (stale) {
    g_canvas.setTextFont(1);
    g_canvas.setTextSize(1);
    g_canvas.setTextColor(rgb565(kRgbWhite), c565(Color::Red));
    g_canvas.fillRect(SCREEN_W - 42, SCREEN_H - BOT_STRIP_H - 14, 38, 10, c565(Color::Red));
    g_canvas.setCursor(SCREEN_W - 39, SCREEN_H - BOT_STRIP_H - 13);
    g_canvas.print("STALE");
  }

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
