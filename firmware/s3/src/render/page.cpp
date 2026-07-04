#include "render/page.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>

#include "proto/parser.h"
#include "render/engine.h"
#include "render/nav_strip.h"
#include "render/palette.h"
#include "render/primitives.h"
#include "render/status_bar.h"
#include "render/warnings.h"
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

bool layout_has_unknown_node(const LayoutNode& node) {
  if (node.type == NodeType::Unknown) return true;
  if (node.type != NodeType::VStack && node.type != NodeType::HStack) return false;
  for (const auto& child : node.as_stack.children) {
    if (layout_has_unknown_node(child)) return true;
  }
  return false;
}

#ifdef ARDUINO

bool parse_page_layout(const PageRecord& rec, LayoutNode& root) {
  JsonDocument doc;
  if (deserializeJson(doc, rec.layout_json)) {
    Serial.println("[render] inner parse failed");
    return false;
  }
  root = parse_layout(doc.as<JsonVariantConst>());
  if (layout_has_unknown_node(root)) {
    Serial.println("[render] layout rejected: unknown node type or invalid color token");
    return false;
  }
  return true;
}

const char* epd_mode_name(epd_mode_t m) {
  switch (m) {
    case epd_mode_t::epd_quality:  return "quality";
    case epd_mode_t::epd_text:     return "text";
    case epd_mode_t::epd_fast:     return "fast";
    case epd_mode_t::epd_fastest:  return "fastest";
    default:                       return "?";
  }
}

static void quantize_canvas_to_e6(M5Canvas& canvas) {
  const int16_t w = canvas.width();
  const int16_t h = canvas.height();
  for (int16_t y = 0; y < h; ++y) {
    for (int16_t x = 0; x < w; ++x) {
      auto px = canvas.readPixelRGB(x, y);
      const uint32_t quantized = quantize_rgb_to_e6(px.R8(), px.G8(), px.B8());
      if (quantized != px.RGB888()) canvas.drawPixel(x, y, quantized);
    }
  }
}

void render_page(const PageRecord& rec, const LayoutNode& root, bool stale,
                 const DeviceState& state,
                 const BatteryReading& batt, const EnvReading& env) {
  // Reset the warning flag so this paint pass starts clean; the badge below
  // only fires if some painter raises a fresh warning during this render.
  render_warn_clear();

  M5GFXMeasurer m(M5.Display);
  int16_t W = (int16_t)M5.Display.width();
  int16_t H = (int16_t)M5.Display.height();
  // Reserve top + bottom strips for the status bar and page-rotation indicator
  // (painted after the page tree), so producer pages can't write into either.
  Rect vp{0, (int16_t)TOP_STRIP_H, W, (int16_t)(H - TOP_STRIP_H - BOT_STRIP_H)};
  auto laid = run_layout(root, vp, m);

#ifdef RCPAPER_DEV_NO_DRAW
  Serial.printf("[render] DEV_NO_DRAW page=%s stale=%d laid=%dx%d\n",
                rec.id.c_str(), stale ? 1 : 0, vp.w, vp.h);
  (void)laid;
#else
  uint32_t t0 = millis();
  if (layout_has_any_image(root)) {
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
  }
  Serial.printf("[render] page=%s stale=%d mode=%s W=%d H=%d draw begin\n",
                rec.id.c_str(), stale ? 1 : 0,
                epd_mode_name(M5.Display.getEpdMode()), W, H);

  // Draw to an offscreen canvas in PSRAM, then push the whole sprite to the
  // panel in one operation. Color e-paper commits per-op when drawing
  // directly on M5.Display — each text/rect would cause its own panel
  // refresh. The canvas keeps all draws in RAM until pushSprite().
  M5Canvas canvas(&M5.Display);
  // 16bpp RGB565 keeps the six E6 ink anchors intact through the canvas.
  // 4bpp indexed mode loses them to the default 16-color palette. Cost:
  // 480 KB in PSRAM, of which we have ~8 MB free.
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  if (!canvas.createSprite(W, H)) {
    Serial.println("[render] canvas alloc FAILED — falling back to direct draw");
    M5.Display.fillScreen(kE6White);
    paint_layout(M5.Display, laid);
    M5.Display.display();
    return;
  }
  canvas.fillSprite(kE6White);
  paint_layout(canvas, laid);

  // Overlay the status bar at the top and page-rotation indicator at the bottom.
  paint_status_bar(canvas, rec, batt, env, state.last_fetch_s);
  paint_indicator(canvas, state.pages, state.page_index);

  // STALE pill (bottom-right) + WARN "!" badge (just left of STALE). The
  // WARN badge fires whenever any painter called render_warn() this pass —
  // visible on the panel so the operator knows to grep the serial log for
  // `[warn]` lines instead of having to re-flash to add prints.
  int16_t pill_right = W - 8;
  if (stale) {
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    const char* label = "STALE";
    int16_t tw = canvas.textWidth(label);
    int16_t pw = tw + 12, ph = 18;
    int16_t px = pill_right - pw;
    int16_t py = H - ph - 6;
    canvas.fillRect(px, py, pw, ph, kE6Red);
    canvas.setTextColor(kE6White);
    canvas.setCursor(px + 6, py + 3);
    canvas.print(label);
    pill_right = px - 6;
  }
  if (render_has_warnings()) {
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    const char* label = "!";
    int16_t tw = canvas.textWidth(label);
    int16_t pw = tw + 12, ph = 18;
    int16_t px = pill_right - pw;
    int16_t py = H - ph - 6;
    canvas.fillRect(px, py, pw, ph, kE6Red);
    canvas.setTextColor(kE6White);
    canvas.setCursor(px + 6, py + 3);
    canvas.print(label);
  }
  quantize_canvas_to_e6(canvas);
  uint32_t t_draw = millis() - t0;
  uint32_t t_push0 = millis();
  canvas.pushSprite(0, 0);
  uint32_t t_push = millis() - t_push0;
  canvas.deleteSprite();

  uint32_t t_disp0 = millis();
  M5.Display.display();
  uint32_t t_disp = millis() - t_disp0;
  Serial.printf("[render] draw=%ums push=%ums display=%ums total=%ums\n",
                (unsigned)t_draw, (unsigned)t_push, (unsigned)t_disp,
                (unsigned)(millis() - t0));
#endif
}

void render_error(const char* msg) {
#ifdef RCPAPER_DEV_NO_DRAW
  Serial.printf("[render] DEV_NO_DRAW error=%s\n", msg);
#else
  // Same offscreen-canvas approach as render_page — keep the error path off
  // the live framebuffer so the panel only refreshes once.
  M5Canvas canvas(&M5.Display);
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  if (!canvas.createSprite(M5.Display.width(), M5.Display.height())) {
    M5.Display.fillScreen(kE6White);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextColor(kE6Black);
    M5.Display.setCursor(20, 40);
    M5.Display.print(msg);
    M5.Display.display();
    return;
  }
  canvas.fillSprite(kE6White);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(kE6Black);
  canvas.setCursor(20, 40);
  canvas.print("rcpapercolor");
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setCursor(20, 80);
  canvas.print(msg);
  quantize_canvas_to_e6(canvas);
  canvas.pushSprite(0, 0);
  canvas.deleteSprite();
  M5.Display.display();
#endif
}

#endif  // ARDUINO

}  // namespace rcpaper
