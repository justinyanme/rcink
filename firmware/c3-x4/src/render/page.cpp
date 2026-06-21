#include "render/page.h"

#include <algorithm>

#ifdef ARDUINO
#include <Arduino.h>
#include <ArduinoJson.h>
#include <EInkDisplay.h>
#include <TFT_eSPI.h>
#include <cstring>
#include <time.h>

#include "config.h"
#include "display/pins_config.h"
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
TFT_eSprite g_canvas(&g_tft);  // 1-bit compose buffer (48KB on the C3)
bool g_canvas_ready = false;

// Vendored SSD1677 driver. Single-buffer mode: the controller's RED RAM holds
// the previous frame for differential fast refresh, so the MCU keeps only one
// 48KB framebuffer alongside the 1-bit compose sprite.
EInkDisplay g_epd(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
bool g_force_full = true;  // first paint after boot is a full refresh (de-ghost)

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

// The 1-bit compose sprite is already in the SSD1677's native framebuffer
// format: row-major, 100 bytes/line, MSB-first, bit=1 -> white. TFT_eSprite
// 1bpp sets a bit for any non-zero colour and clears it for 0x0000; with the
// B&W palette (white=0xFFFF, every other ink=0x0000) that is exactly the panel's
// convention, so the pack is a straight memcpy. The first paint after boot is a
// full refresh (de-ghost); later paints use the faster differential refresh.
void push_canvas(bool full = false) {
  if (!g_canvas_ready) return;
  std::memcpy(g_epd.getFrameBuffer(), g_canvas.getPointer(), EInkDisplay::BUFFER_SIZE);
  const EInkDisplay::RefreshMode mode =
      (full || g_force_full) ? EInkDisplay::FULL_REFRESH : EInkDisplay::FAST_REFRESH;
  g_epd.displayBuffer(mode);
  g_force_full = false;
}

void draw_battery(int16_t x, int16_t y, const BatteryReading& batt) {
  constexpr int16_t w = 44;
  constexpr int16_t h = 20;
  g_canvas.drawRect(x, y, w, h, c565(Color::Black));
  g_canvas.fillRect(x + w, y + 6, 3, 8, c565(Color::Black));  // terminal nub
  if (!batt.ok) return;
  Color fill = Color::Green;
  if (batt.pct < 30) fill = Color::Yellow;
  if (batt.pct < 10) fill = Color::Red;
  const int16_t fill_w = (int16_t)((w - 4) * batt.pct / 100);
  if (fill_w > 0) g_canvas.fillRect(x + 2, y + 2, fill_w, h - 4, c565(fill));
}

// Page-position rail: a scrollbar "thumb" folded onto the content/sidebar
// divider. The thumb is 1/N of the track tall and slides top->bottom as the
// deck advances, so position-in-deck reads at a glance without spending a
// status-block row on "N/M" text. Drawn in Black so the dark-swap lights it
// white against the thin divider line. Skipped for single-page decks (nothing
// to page through). Geometry mirrors tools/preview_sidebar.py (the oracle).
void draw_page_rail(int16_t divider_x, size_t count, size_t index) {
  if (count <= 1) return;
  constexpr int16_t kTop = 8;
  constexpr int16_t kBot = SCREEN_H - 100;  // stay clear of the PREV/NEXT block
  const int16_t track = kBot - kTop;
  int16_t th = (int16_t)(track / (int16_t)count);
  if (th < 6) th = 6;
  int16_t ty = (int16_t)(kTop + (int32_t)track * (int32_t)index / (int32_t)count);
  if (ty + th > kBot) ty = (int16_t)(kBot - th);
  g_canvas.fillRect((int16_t)(divider_x - 2), ty, 5, th, c565(Color::Black));
}

// Right-edge chrome strip, next to the two physical buttons. Top block =
// status (battery, clock) with a page-position rail on the divider; bottom
// block = prev/next hints aligned to the buttons. STALE shows as a red chip
// when the cache is past its window.
void paint_sidebar(const DeviceState& state, const BatteryReading& batt,
                   uint64_t last_fetch_s, bool stale) {
  const int16_t x0 = SCREEN_W - SIDEBAR_W;  // 690
  const int16_t cx = x0 + 9;                // text inset inside the sidebar
  g_canvas.fillRect(x0, 0, SIDEBAR_W, SCREEN_H, rgb565(kRgbWhite));
  g_canvas.drawFastVLine(x0, 0, SCREEN_H, c565(Color::Black));

  // Size-2 GLCD (12x16) — the size-1 chrome was tiny next to the big X4 page
  // content. Rows are spaced for the 480px panel.
  g_canvas.setTextFont(1);
  g_canvas.setTextSize(2);
  g_canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));

  // --- status block (top): battery, weekday + date, clock; page-position
  //     rail rides the divider (drawn below) instead of a "N/M" text row ---
  draw_battery(cx, 14, batt);
  char pct[8];
  if (batt.ok) snprintf(pct, sizeof(pct), "%u%%", (unsigned)batt.pct);
  else snprintf(pct, sizeof(pct), "--");
  g_canvas.setCursor(cx, 44);
  g_canvas.print(pct);

  g_canvas.setCursor(cx, 78);
  g_canvas.print(format_local_wday(last_fetch_s));
  g_canvas.setCursor(cx, 104);
  g_canvas.print(format_local_md(last_fetch_s));
  g_canvas.setCursor(cx, 130);
  g_canvas.print(format_local_hhmm(last_fetch_s));

  draw_page_rail(x0, state.pages.size(), state.page_index);

  if (stale) {
    g_canvas.fillRect(cx, 166, 78, 22, c565(Color::Red));
    g_canvas.setTextColor(rgb565(kRgbWhite), c565(Color::Red));
    g_canvas.setCursor(cx + 5, 169);
    g_canvas.print("STALE");
    g_canvas.setTextColor(c565(Color::Black), rgb565(kRgbWhite));
  }

  // --- nav hints (bottom, by the buttons): PREV on top, NEXT below ---
  const int16_t prev_y = SCREEN_H - 86;
  const int16_t next_y = SCREEN_H - 42;
  g_canvas.fillTriangle(cx + 8, prev_y, cx, prev_y + 13, cx + 16, prev_y + 13, c565(Color::Black));
  g_canvas.setCursor(cx + 24, prev_y);
  g_canvas.print("PREV");
  g_canvas.fillTriangle(cx, next_y, cx + 16, next_y, cx + 8, next_y + 13, c565(Color::Black));
  g_canvas.setCursor(cx + 24, next_y);
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

// Clamp every node's rect to the content width. Text nodes size to their content
// and can be wider than the viewport (a long label row, or the overview's quote
// masthead on the wide 800px panel). Clamping makes a too-wide leaf truncate via
// draw_text's ellipsis instead of forcing a whole-page width shrink (which would
// drag every font down). Full-width bars sit exactly at CONTENT_W and are
// unaffected.
void clamp_to_width(LaidOutNode& n, int16_t max_right) {
  if (n.rect.x >= max_right) {
    n.rect.w = 0;
  } else if (n.rect.x + n.rect.w > max_right) {
    n.rect.w = (int16_t)(max_right - n.rect.x);
  }
  for (auto& c : n.children) clamp_to_width(c, max_right);
}

}  // namespace

void display_init() {
  Serial.println("[display] ssd1677 init");
  g_epd.begin();                                  // X4 default geometry: 800x480
  g_epd.clearScreen(0xFF);                         // all-white framebuffer
  g_epd.displayBuffer(EInkDisplay::FULL_REFRESH);  // clear the panel + de-ghost

  g_canvas.setColorDepth(1);  // 1-bit compose buffer (48KB; the C3 has no PSRAM)
  g_canvas_ready = g_canvas.createSprite(SCREEN_W, SCREEN_H) != nullptr;
  if (!g_canvas_ready) {
    Serial.println("[display] sprite alloc FAILED");
    return;
  }
  fonts_begin(g_canvas);  // bind the u8g2 CJK renderer to the sprite buffer
  g_canvas.fillSprite(rgb565(kRgbWhite));
  // No push here: the panel is already white from the full refresh above. The
  // first content render does the first full-refresh push.
  Serial.printf("[display] ready %dx%d free_heap=%u\n",
                SCREEN_W, SCREEN_H, (unsigned)ESP.getFreeHeap());
}

// The C3 has no PSRAM and can't hold the 48KB compose sprite, the ~40KB TLS
// buffers and the ~38KB Axiom response at once. fetch_and_restart() frees the
// sprite for the duration of the fetch with this call. It does NOT rebuild it
// on success — the heap is too fragmented afterwards to reclaim a 48KB
// contiguous block — and instead reboots to render from the saved NVS cache on
// a clean heap. acquire is only used to recover for an error message on failure.
void display_release_canvas() {
  if (!g_canvas_ready) return;
  g_canvas.deleteSprite();
  g_canvas_ready = false;
}

void display_acquire_canvas() {
  if (g_canvas_ready) return;
  g_canvas.setColorDepth(1);
  g_canvas_ready = g_canvas.createSprite(SCREEN_W, SCREEN_H) != nullptr;
  if (!g_canvas_ready) {
    Serial.println("[display] sprite re-alloc FAILED");
    return;
  }
  fonts_begin(g_canvas);
  g_canvas.fillSprite(rgb565(kRgbWhite));
}

void display_sleep() {
  g_epd.deepSleep();
}

void clear_screen() {
  if (!g_canvas_ready) return;
  g_canvas.fillSprite(rgb565(kRgbWhite));
  push_canvas(true);  // full refresh for a clean wipe
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

  // Keep the chosen font sizes: render at scale 1.0 and only shrink if leaf
  // content genuinely runs past the BOTTOM (the root container fills the panel,
  // so content_bottom == SCREEN_H normally). WIDTH overflow is NOT handled by
  // shrinking the whole page — that would drag every font down whenever one row
  // is a little too wide. Instead the laid-out tree is clamped to the content
  // width below, so a too-wide leaf (a long label/quote) truncates via
  // draw_text's ellipsis while everything else stays full size.
  constexpr float kSpacing = 0.7f;
  constexpr float kMinScale = 0.45f;
  float scale = 1.0f;
  set_text_scale(scale);
  auto laid = run_layout(root, vp, m, scale * kSpacing);
  int16_t used = (int16_t)(content_bottom(laid) - vp.y);
  for (int pass = 0; pass < 3 && scale > kMinScale && used > SCREEN_H; ++pass) {
    scale *= (float)SCREEN_H / (float)used * 0.97f;
    if (scale < kMinScale) scale = kMinScale;
    set_text_scale(scale);
    laid = run_layout(root, vp, m, scale * kSpacing);
    used = (int16_t)(content_bottom(laid) - vp.y);
  }
  clamp_to_width(laid, CONTENT_W);

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

// Dump the 1-bit compose buffer over serial as hex, framed by markers, so the
// host can reconstruct a PNG (tools/fb_to_png.py) without a camera. The buffer
// is already the panel's native format: 100 bytes/line, MSB-first, bit=1=white.
void dump_framebuffer() {
  if (!g_canvas_ready) {
    Serial.println("[fb] not ready");
    return;
  }
  const uint8_t* buf = (const uint8_t*)g_canvas.getPointer();
  constexpr int stride = SCREEN_W / 8;  // 100 bytes per line
  static const char kHex[] = "0123456789abcdef";
  char line[2 * stride + 1];
  Serial.printf("[fb] BEGIN %dx%d 1bpp\n", SCREEN_W, SCREEN_H);
  for (int y = 0; y < SCREEN_H; ++y) {
    const uint8_t* row = buf + (size_t)y * stride;
    for (int b = 0; b < stride; ++b) {
      line[2 * b] = kHex[row[b] >> 4];
      line[2 * b + 1] = kHex[row[b] & 0x0F];
    }
    line[2 * stride] = 0;
    Serial.println(line);
    Serial.flush();  // drain the USB-CDC FIFO so the big dump can't overflow it
  }
  Serial.println("[fb] END");
}

#endif  // ARDUINO

}  // namespace rcpaper
