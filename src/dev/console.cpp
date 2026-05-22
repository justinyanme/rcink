#include "dev/console.h"

#ifdef RCPAPER_DEV_CONSOLE
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_system.h>
#include <time.h>

#include "net/wifi.h"
#include "power/sleep.h"
#include "render/page.h"
#include "render/palette.h"
#include "status/led_status.h"
#include "store/nvs.h"

namespace rcpaper {
// Orchestrator state + entry points; defined in main.cpp.
extern DeviceState g_state;
bool refetch_now();
void render_current();
}  // namespace rcpaper

namespace rcpaper::dev {

void console_help() {
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

void dev_buttons_init() {
  for (auto& b : g_dev_btns) pinMode(b.pin, INPUT_PULLUP);
}

void dev_buttons_poll() {
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

void console_dispatch(char c) {
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
      led_status_show(LedState::IdleDisconnected);
      break;
    case 'f':
      Serial.println("[console] f → refetch");
      if (refetch_now()) render_current();
      if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
      led_status_show(LedState::IdleDisconnected);
      break;
    case 'n':
      if (g_state.pages.empty()) { Serial.println("[console] no pages"); break; }
      g_state.page_index = (g_state.page_index + 1) % g_state.pages.size();
      nvs_save_progress(g_state.page_index, g_state.wakes_since_fetch);
      Serial.printf("[console] n → idx=%u\n", g_state.page_index);
      render_current();
      led_status_show(LedState::IdleDisconnected);
      break;
    case 'b':
      if (g_state.pages.empty()) { Serial.println("[console] no pages"); break; }
      g_state.page_index = (g_state.page_index + g_state.pages.size() - 1) % g_state.pages.size();
      nvs_save_progress(g_state.page_index, g_state.wakes_since_fetch);
      Serial.printf("[console] b → idx=%u\n", g_state.page_index);
      render_current();
      led_status_show(LedState::IdleDisconnected);
      break;
    case 'c': {
      Serial.println("[console] c → clear screen");
      uint32_t t0 = millis();
      M5.Display.startWrite();
      M5.Display.fillScreen(kE6White);
      M5.Display.endWrite();
      M5.Display.display();
      Serial.printf("[console] clear done in %u ms\n", (unsigned)(millis() - t0));
      led_status_show(LedState::IdleDisconnected);
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

}  // namespace rcpaper::dev

#endif  // RCPAPER_DEV_CONSOLE
