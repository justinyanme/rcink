// src/main.cpp
#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_system.h>
#include <time.h>

#include "config.h"
#include "net/wifi.h"
#include "net/axiom.h"
#include "proto/parser.h"
#include "render/engine.h"
#include "store/nvs.h"
#include "power/sleep.h"

using namespace rcpaper;

// Persistent across the dev-console loop. Loaded once in setup().
static DeviceState g_state;

static void apply_local_tz() {
  setenv("TZ", LOCAL_TZ, 1);
  tzset();
}

static void sync_ntp_once() {
  // pool.ntp.org is often blocked on Chinese networks; try aliyun first.
  configTime(0, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
  // Apply local TZ so subsequent localtime_r() calls (e.g. for the footer
  // HH:MM stamps) render in the user's wall-clock instead of UTC.
  apply_local_tz();
  struct tm ti;
  int i;
  for (i = 0; i < 20 && !getLocalTime(&ti); ++i) delay(500);
  if (i >= 20) {
    Serial.println("[ntp] sync failed (timeout)");
  } else {
    Serial.printf("[ntp] synced after %d iters; now=%04d-%02d-%02d %02d:%02d:%02d (tz=%s)\n",
                  i, ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                  ti.tm_hour, ti.tm_min, ti.tm_sec, LOCAL_TZ);
  }
}

static const char* epd_mode_name(epd_mode_t m) {
  switch (m) {
    case epd_mode_t::epd_quality:  return "quality";
    case epd_mode_t::epd_text:     return "text";
    case epd_mode_t::epd_fast:     return "fast";
    case epd_mode_t::epd_fastest:  return "fastest";
    default:                       return "?";
  }
}

// Two-row strip at the bottom of the canvas:
//
//   ─────────────────────────────────────────────
//                  data 14:32 → refresh 14:45
//   ◀ PREV-ID         ● ○ ○         NEXT-ID ▶
//
// Top row (TS_H px): producer build time vs. firmware fetch time, drawn from
// the current page's data_ts and DeviceState::last_fetch_s.
// Bottom row (NAV_H px): rotation cursor — filled dot is current, hollow others;
// PREV-ID and NEXT-ID are the IDs reached by BtnB / BtnA. Sits flush against
// the bottom of the visible panel; the M5PaperColor bezel clips a few pixels
// below the dot row, so the nav row must be the lowest content.
//
// Drawn straight on the M5Canvas so it composes with the page tree underneath
// — producers don't need to reserve space, but content drawn into the bottom
// IND_STRIP_H pixels is covered.
static constexpr int TS_H  = 14;
static constexpr int NAV_H = 22;
static constexpr int IND_STRIP_H = TS_H + NAV_H;

static String format_local_hhmm(uint64_t epoch_s) {
  if (epoch_s == 0) return String("--:--");
  // Both producer data_ts and firmware last_fetch_s are stored as UTC epoch
  // seconds. LOCAL_TZ is a display rule only, applied identically here.
  apply_local_tz();
  time_t t = (time_t)epoch_s;
  struct tm tm {};
  if (!localtime_r(&t, &tm)) return String("--:--");
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
  return String(buf);
}

static void paint_indicator(M5Canvas& canvas,
                            const std::vector<PageRecord>& pages, uint8_t idx,
                            uint64_t last_fetch_s) {
  if (pages.empty()) return;
  const int16_t W = canvas.width();
  const int16_t H = canvas.height();
  const int16_t y_strip = H - IND_STRIP_H;
  // Timestamps on top of the strip so they sit above the bezel-clipped area;
  // the nav row is flush with the bottom of the visible panel.
  const int16_t y_ts    = y_strip;
  const int16_t y_nav   = y_strip + TS_H;

  // Solid white background + hairline top rule so the strip floats cleanly
  // above any page content drawn behind it.
  canvas.fillRect(0, y_strip, W, IND_STRIP_H, 0xFFFFFF);
  canvas.drawFastHLine(0, y_strip, W, 0x000000);

  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(0x000000);

  const size_t n = pages.size();
  const uint8_t prev_idx = (uint8_t)((idx + n - 1) % n);
  const uint8_t next_idx = (uint8_t)((idx + 1) % n);

  auto upper = [](const String& s) {
    String out = s;
    out.toUpperCase();
    return out;
  };
  const String prev_id = upper(pages[prev_idx].id);
  const String next_id = upper(pages[next_idx].id);

  const int16_t nav_mid = y_nav + NAV_H / 2;

  // Left: ◀  PREV
  const int16_t lpad = 8;
  canvas.fillTriangle(lpad,     nav_mid,
                      lpad + 7, y_nav + 4,
                      lpad + 7, y_nav + NAV_H - 4, 0x000000);
  canvas.setCursor(lpad + 13, y_nav + 3);
  canvas.print(prev_id.c_str());

  // Right: NEXT  ▶
  const int16_t rpad = 8;
  const int16_t arrow_x = W - rpad - 7;
  canvas.fillTriangle(arrow_x,     y_nav + 4,
                      arrow_x,     y_nav + NAV_H - 4,
                      arrow_x + 7, nav_mid, 0x000000);
  const int16_t next_w = canvas.textWidth(next_id.c_str());
  canvas.setCursor(arrow_x - 5 - next_w, y_nav + 3);
  canvas.print(next_id.c_str());

  // Center: dot pagination. Spacing chosen so even 8 pages stay comfortably
  // within the middle gap between the prev/next labels.
  const int16_t dot_gap = 12;
  const int16_t dots_w = (int16_t)((n - 1) * dot_gap);
  int16_t dx = W / 2 - dots_w / 2;
  for (size_t i = 0; i < n; ++i) {
    if (i == idx) canvas.fillCircle(dx, nav_mid, 3, 0x000000);
    else          canvas.drawCircle(dx, nav_mid, 2, 0x000000);
    dx += dot_gap;
  }

  // Bottom row: data HH:MM → refresh HH:MM, centered.
  const PageRecord& rec = pages[idx];
  String line = "data ";
  line += format_local_hhmm(rec.data_ts);
  line += "  ->  refresh ";
  line += format_local_hhmm(last_fetch_s);

  canvas.setFont(&fonts::FreeSans9pt7b);
  int16_t tw = canvas.textWidth(line.c_str());
  canvas.setCursor((W - tw) / 2, y_ts + 1);
  canvas.print(line.c_str());
}

static bool layout_has_remote_image(const LayoutNode& node) {
  if (node.type == NodeType::Image && !node.as_image.url.empty()) return true;
  if (node.type != NodeType::VStack && node.type != NodeType::HStack) return false;
  for (const auto& child : node.as_stack.children) {
    if (layout_has_remote_image(child)) return true;
  }
  return false;
}

static bool layout_has_any_image(const LayoutNode& node) {
  if (node.type == NodeType::Image) return true;
  if (node.type != NodeType::VStack && node.type != NodeType::HStack) return false;
  for (const auto& child : node.as_stack.children) {
    if (layout_has_any_image(child)) return true;
  }
  return false;
}

static bool parse_page_layout(const PageRecord& rec, LayoutNode& root) {
  JsonDocument doc;
  if (deserializeJson(doc, rec.layout_json)) {
    Serial.println("[render] inner parse failed");
    return false;
  }
  root = parse_layout(doc.as<JsonVariantConst>());
  return true;
}

static void render_page(const PageRecord& rec, const LayoutNode& root, bool stale) {

  M5GFXMeasurer m(M5.Display);
  int16_t W = (int16_t)M5.Display.width();
  int16_t H = (int16_t)M5.Display.height();
  // Reserve the bottom strip for the page-rotation indicator (painted after
  // the page tree), so producer pages can't write into that band.
  Rect vp{0, 0, W, (int16_t)(H - IND_STRIP_H)};
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
  // 16bpp RGB565: keeps our muted brand colors (0xD92027, 0x0F4D92, ...)
  // intact through the canvas. 4bpp indexed mode loses them to the default
  // 16-color palette. Cost: 480 KB in PSRAM, of which we have ~8 MB free.
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  if (!canvas.createSprite(W, H)) {
    Serial.println("[render] canvas alloc FAILED — falling back to direct draw");
    M5.Display.fillScreen(0xFFFFFF);
    paint_layout(M5.Display, laid);
    M5.Display.display();
    return;
  }
  canvas.fillSprite(0xFFFFFF);
  paint_layout(canvas, laid);

  // Overlay the page-rotation indicator strip at the bottom of every page.
  paint_indicator(canvas, g_state.pages, g_state.page_index,
                  g_state.last_fetch_s);

  if (stale) {
    // "STALE" pill in the bottom-right
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    const char* label = "STALE";
    int16_t tw = canvas.textWidth(label);
    int16_t pw = tw + 12, ph = 18;
    int16_t px = W - pw - 8;
    int16_t py = H - ph - 6;
    canvas.fillRect(px, py, pw, ph, 0xD92027);
    canvas.setTextColor(0xFFFFFF);
    canvas.setCursor(px + 6, py + 3);
    canvas.print(label);
  }
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

static void render_error(const char* msg) {
#ifdef RCPAPER_DEV_NO_DRAW
  Serial.printf("[render] DEV_NO_DRAW error=%s\n", msg);
#else
  // Same offscreen-canvas approach as render_page — keep the error path off
  // the live framebuffer so the panel only refreshes once.
  M5Canvas canvas(&M5.Display);
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  if (!canvas.createSprite(M5.Display.width(), M5.Display.height())) {
    M5.Display.fillScreen(0xFFFFFF);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextColor(0x000000);
    M5.Display.setCursor(20, 40);
    M5.Display.print(msg);
    M5.Display.display();
    return;
  }
  canvas.fillSprite(0xFFFFFF);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(0x000000);
  canvas.setCursor(20, 40);
  canvas.print("rcpapercolor");
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setCursor(20, 80);
  canvas.print(msg);
  canvas.pushSprite(0, 0);
  canvas.deleteSprite();
  M5.Display.display();
#endif
}

// ---- shared cycle steps ---------------------------------------------------

static bool refetch_now() {
  if (!wifi_connect()) {
    Serial.println("[main] wifi failed");
    return false;
  }
  sync_ntp_once();
  std::vector<PageRecord> fresh;
  bool ok = axiom_fetch_pages(fresh) && !fresh.empty();
  if (ok) {
    g_state.pages        = std::move(fresh);
    g_state.last_fetch_s = (uint64_t)time(nullptr);
    g_state.page_index   = 0;
    nvs_save(g_state);
    Serial.printf("[main] fetched %u pages, idx=0\n", (unsigned)g_state.pages.size());
  } else {
    Serial.println("[main] fetch failed; keeping cache");
  }
  return ok;
}

static void render_current() {
  if (g_state.pages.empty()) {
    render_error("no pages — check producer + Axiom dataset");
    return;
  }
  const PageRecord& rec = g_state.pages[g_state.page_index];
  LayoutNode root;
  if (!parse_page_layout(rec, root)) {
    render_error("layout_json parse failed");
    return;
  }
  bool connected_here = false;
  if (layout_has_remote_image(root) && WiFi.status() != WL_CONNECTED) {
    connected_here = wifi_connect();
    if (!connected_here) {
      Serial.println("[render] wifi unavailable for remote image");
    }
  }
  bool stale = (uint64_t)time(nullptr) - g_state.last_fetch_s > FETCH_INTERVAL_SEC;
  render_page(rec, root, stale);
  if (connected_here) {
    wifi_disconnect();
  }
}

// ---- USB-CDC dev console --------------------------------------------------
//
// Sit in loop() reading single chars from Serial; dispatch to firmware ops.
// Lets the Mac drive the device end-to-end without buttons. Enabled by
// RCPAPER_DEV_CONSOLE — when off, setup() ends in deep_sleep as before.

#ifdef RCPAPER_DEV_CONSOLE

static void console_help() {
  Serial.println(F("[console] commands:"));
  Serial.println(F("  f  refetch from Axiom + persist + render"));
  Serial.println(F("  r  re-render current page from cache"));
  Serial.println(F("  n  next page"));
  Serial.println(F("  b  previous page"));
  Serial.println(F("  c  clear screen (white)"));
  Serial.println(F("  s  state dump"));
  Serial.println(F("  q  dump cached layout_json for current page"));
  Serial.println(F("  m  cycle EPD mode (quality→text→fast→fastest)"));
  Serial.println(F("  D  enter deep sleep (10 s)"));
  Serial.println(F("  R  reboot now"));
  Serial.println(F("  h  this help"));
}

static void console_state_dump() {
  Serial.printf("[state] pages=%u idx=%u last_fetch=%llu epd=%s heap=%u\n",
                (unsigned)g_state.pages.size(),
                (unsigned)g_state.page_index,
                (unsigned long long)g_state.last_fetch_s,
                epd_mode_name(M5.Display.getEpdMode()),
                (unsigned)esp_get_free_heap_size());
  for (size_t i = 0; i < g_state.pages.size(); ++i) {
    Serial.printf("  [%u] id=%s order=%d layout=%u B\n",
                  (unsigned)i,
                  g_state.pages[i].id.c_str(),
                  g_state.pages[i].order,
                  (unsigned)g_state.pages[i].layout_json.length());
  }
}

static void console_dispatch(char c);  // forward decl for dev_buttons_poll

// In production the buttons act as EXT1 deep-sleep wake sources, so the device
// wakes → fetches/rotates → renders → sleeps in one cycle. In DEV_CONSOLE mode
// the device stays awake and never sleeps, so those wake sources are inert.
// Poll the three side buttons (M5Unified mapping: G9=A, G10=B, G1=C) directly
// in loop() and dispatch the matching console command on a falling edge so the
// device behaves the same as a deep-sleep cycle from the user's POV.
struct DevBtn { int pin; char cmd; bool prev_high; uint32_t last_change_ms; };
static DevBtn g_dev_btns[] = {
  { 9,  'n', true, 0 },   // BtnA → next page
  { 10, 'b', true, 0 },   // BtnB → previous page
  { 1,  'f', true, 0 },   // BtnC → force refetch
};

static void dev_buttons_init() {
  for (auto& b : g_dev_btns) pinMode(b.pin, INPUT_PULLUP);
}

static void dev_buttons_poll() {
  uint32_t now = millis();
  for (auto& b : g_dev_btns) {
    bool high = digitalRead(b.pin) != 0;
    if (high == b.prev_high) continue;
    // 30 ms debounce: a real press is much longer than any contact bounce.
    if (now - b.last_change_ms < 30) continue;
    b.prev_high = high;
    b.last_change_ms = now;
    if (!high) {  // falling edge = press
      Serial.printf("[btn] G%d → '%c'\n", b.pin, b.cmd);
      console_dispatch(b.cmd);
    }
  }
}

static void console_dispatch(char c) {
  switch (c) {
    case 'h': case '?':
      console_help();
      break;
    case 's':
      console_state_dump();
      break;
    case 'q':
      if (g_state.pages.empty()) {
        Serial.println("[console] no pages");
      } else {
        const auto& rec = g_state.pages[g_state.page_index];
        Serial.printf("[console] layout_json id=%s len=%u:\n%s\n",
                      rec.id.c_str(), (unsigned)rec.layout_json.length(),
                      rec.layout_json.c_str());
      }
      break;
    case 'r':
      Serial.println("[console] r → re-render current");
      render_current();
      break;
    case 'f':
      Serial.println("[console] f → refetch");
      if (refetch_now()) render_current();
      if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
      break;
    case 'n':
      if (g_state.pages.empty()) { Serial.println("[console] no pages"); break; }
      g_state.page_index = (g_state.page_index + 1) % g_state.pages.size();
      nvs_save_index(g_state.page_index);
      Serial.printf("[console] n → idx=%u\n", g_state.page_index);
      render_current();
      break;
    case 'b':
      if (g_state.pages.empty()) { Serial.println("[console] no pages"); break; }
      g_state.page_index = (g_state.page_index + g_state.pages.size() - 1) % g_state.pages.size();
      nvs_save_index(g_state.page_index);
      Serial.printf("[console] b → idx=%u\n", g_state.page_index);
      render_current();
      break;
    case 'c': {
      Serial.println("[console] c → clear screen");
      uint32_t t0 = millis();
      M5.Display.startWrite();
      M5.Display.fillScreen(0xFFFFFF);
      M5.Display.endWrite();
      M5.Display.display();
      Serial.printf("[console] clear done in %u ms\n", (unsigned)(millis() - t0));
      break;
    }
    case 'm': {
      // quality → text → fast → fastest → quality
      epd_mode_t cur = M5.Display.getEpdMode();
      epd_mode_t nxt = (cur == epd_mode_t::epd_quality) ? epd_mode_t::epd_text :
                       (cur == epd_mode_t::epd_text)    ? epd_mode_t::epd_fast :
                       (cur == epd_mode_t::epd_fast)    ? epd_mode_t::epd_fastest :
                                                          epd_mode_t::epd_quality;
      M5.Display.setEpdMode(nxt);
      Serial.printf("[console] m → epd_mode %s → %s\n",
                    epd_mode_name(cur), epd_mode_name(nxt));
      break;
    }
    case 'D':
      Serial.println("[console] D → deep sleep 10s");
      enter_deep_sleep_until((uint64_t)time(nullptr) + 10);
      break;
    case 'R':
      Serial.println("[console] R → esp_restart");
      Serial.flush();
      delay(50);
      esp_restart();
      break;
    case '\n': case '\r': case ' ': case 0:
      break;
    default:
      Serial.printf("[console] unknown char 0x%02x (try 'h')\n", (unsigned)(uint8_t)c);
      break;
  }
}

#endif  // RCPAPER_DEV_CONSOLE

// ---- entry points ---------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(50);

  auto cfg = M5.config();
#ifdef RCPAPER_DEV_NO_DRAW
  cfg.clear_display = false;
#else
  cfg.clear_display = true;
#endif
  cfg.output_power  = true;
  M5.begin(cfg);
  // epd_quality buffers all draws into M5GFX's offscreen buffer and commits
  // them in one full-panel refresh on Display.display(). epd_text auto-pushes
  // after each text write, which on a 7-color panel manifests as one full
  // refresh cycle per element drawn.
  M5.Display.setEpdMode(epd_mode_t::epd_quality);

  // Apply local TZ once per boot. TZ env survives within a process but not
  // across esp_restart/deep-sleep cycles, so set it before any localtime_r()
  // call (e.g. on cache-only wakes that skip NTP).
  apply_local_tz();

  WakeReason wr = current_wake_reason();
  Serial.printf("[main] wake reason = %u\n", (unsigned)wr);

  g_state = nvs_load();
  time_t now;
  time(&now);

  bool needs_fetch =
      g_state.pages.empty()
      || wr == WakeReason::ButtonC
      || (uint64_t)now - g_state.last_fetch_s >= FETCH_INTERVAL_SEC;

  if (needs_fetch) {
    refetch_now();
  }

  // Rotate based on wake reason
  if (!g_state.pages.empty()) {
    if (wr == WakeReason::ButtonA || wr == WakeReason::Timer) {
      g_state.page_index = (uint8_t)((g_state.page_index + 1) % g_state.pages.size());
    } else if (wr == WakeReason::ButtonB) {
      g_state.page_index = (uint8_t)((g_state.page_index + g_state.pages.size() - 1)
                                      % g_state.pages.size());
    }
    nvs_save_index(g_state.page_index);
  }

  render_current();
  if (WiFi.status() == WL_CONNECTED) {
    wifi_disconnect();
  }

#ifdef RCPAPER_DEV_CONSOLE
  dev_buttons_init();
  Serial.println("[console] ready — type 'h' for commands. Deep sleep disabled.");
#else
  // Production path: compute next wake and deep sleep.
  uint64_t next_fetch  = g_state.last_fetch_s + FETCH_INTERVAL_SEC;
  uint64_t next_rotate = (uint64_t)time(nullptr) + ROTATE_INTERVAL_SEC;
  uint64_t next_wake   = next_fetch < next_rotate ? next_fetch : next_rotate;
  enter_deep_sleep_until(next_wake);
#endif
}

void loop() {
#ifdef RCPAPER_DEV_CONSOLE
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    console_dispatch((char)c);
  }
  dev_buttons_poll();
  delay(20);
#endif
}
