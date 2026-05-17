// src/main.cpp
#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <time.h>

#include "config.h"
#include "net/wifi.h"
#include "net/axiom.h"
#include "proto/parser.h"
#include "render/engine.h"
#include "store/nvs.h"
#include "power/sleep.h"

using namespace rcpaper;

static void sync_ntp_once() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm ti;
  for (int i = 0; i < 20 && !getLocalTime(&ti); ++i) delay(500);
}

static void render_page(const PageRecord& rec, bool stale) {
  JsonDocument doc;
  if (deserializeJson(doc, rec.layout_json)) {
    Serial.println("[render] inner parse failed");
    return;
  }
  LayoutNode root = parse_layout(doc.as<JsonVariantConst>());

  M5GFXMeasurer m(M5.Display);
  M5.Display.fillScreen(0xFFFFFF);
  Rect vp{0, 0, (int16_t)M5.Display.width(), (int16_t)M5.Display.height()};

  auto laid = run_layout(root, vp, m);
  paint_layout(M5.Display, laid);

  if (stale) {
    // "STALE" pill in the bottom-right
    M5.Display.setFont(&fonts::FreeSansBold9pt7b);
    const char* label = "STALE";
    int16_t tw = M5.Display.textWidth(label);
    int16_t pw = tw + 12, ph = 18;
    int16_t px = vp.w - pw - 8;
    int16_t py = vp.h - ph - 6;
    M5.Display.fillRect(px, py, pw, ph, 0xD92027);   // red bg
    M5.Display.setTextColor(0xFFFFFF);
    M5.Display.setCursor(px + 6, py + 3);
    M5.Display.print(label);
  }
  M5.Display.display();
}

static void render_error(const char* msg) {
  M5.Display.fillScreen(0xFFFFFF);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(0x000000);
  M5.Display.setCursor(20, 40);
  M5.Display.print("rcpapercolor");
  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.setCursor(20, 80);
  M5.Display.print(msg);
  M5.Display.display();
}

void setup() {
  Serial.begin(115200);
  delay(50);

  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power  = true;
  M5.begin(cfg);
  M5.Display.setEpdMode(epd_mode_t::epd_text);

  WakeReason wr = current_wake_reason();
  Serial.printf("[main] wake reason = %u\n", (unsigned)wr);

  DeviceState state = nvs_load();
  time_t now;
  time(&now);

  bool needs_fetch =
      state.pages.empty()
      || wr == WakeReason::ButtonC
      || (uint64_t)now - state.last_fetch_s >= FETCH_INTERVAL_SEC;

  if (needs_fetch) {
    if (!wifi_connect()) {
      render_error("WiFi failed; keeping cached pages");
    } else {
      sync_ntp_once();
      std::vector<PageRecord> fresh;
      if (axiom_fetch_pages(fresh) && !fresh.empty()) {
        state.pages        = std::move(fresh);
        state.last_fetch_s = (uint64_t)time(nullptr);
        state.page_index   = 0;  // land on first page after fetch
        nvs_save(state);
      } else {
        Serial.println("[main] fetch failed; using cache");
      }
      wifi_disconnect();
    }
  }

  // Rotate based on wake reason
  if (!state.pages.empty()) {
    if (wr == WakeReason::ButtonA || wr == WakeReason::Timer) {
      state.page_index = (uint8_t)((state.page_index + 1) % state.pages.size());
    } else if (wr == WakeReason::ButtonB) {
      state.page_index = (uint8_t)((state.page_index + state.pages.size() - 1)
                                    % state.pages.size());
    }
    nvs_save_index(state.page_index);
  }

  // Render
  if (state.pages.empty()) {
    render_error("no pages — check producer + Axiom dataset");
  } else {
    bool stale = (uint64_t)time(nullptr) - state.last_fetch_s > FETCH_INTERVAL_SEC;
    render_page(state.pages[state.page_index], stale);
  }

  // Compute next wake
  uint64_t next_fetch  = state.last_fetch_s + FETCH_INTERVAL_SEC;
  uint64_t next_rotate = (uint64_t)time(nullptr) + ROTATE_INTERVAL_SEC;
  uint64_t next_wake   = next_fetch < next_rotate ? next_fetch : next_rotate;

  enter_deep_sleep_until(next_wake);
}

void loop() { /* unreachable: setup() ends in deep sleep */ }
