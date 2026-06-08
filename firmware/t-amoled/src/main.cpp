#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <time.h>

#include "config.h"
#include "display/pins_config.h"
#include "net/axiom.h"
#include "net/wifi.h"
#include "proto/parser.h"
#include "render/page.h"
#include "sense/battery.h"
#include "store/nvs.h"

namespace rcpaper {

DeviceState g_state;
BatteryReading g_battery;

static void apply_local_tz() {
  setenv("TZ", LOCAL_TZ, 1);
  tzset();
}

static bool sync_ntp_once() {
  configTime(0, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
  apply_local_tz();
  const uint32_t start = millis();
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    if (millis() - start > 10000) {
      Serial.println("[ntp] sync timeout");
      return false;
    }
    delay(100);
  }
  time_t now;
  struct tm ti;
  time(&now);
  localtime_r(&now, &ti);
  Serial.printf("[ntp] %04d-%02d-%02d %02d:%02d:%02d tz=%s\n",
                ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                ti.tm_hour, ti.tm_min, ti.tm_sec, LOCAL_TZ);
  return true;
}

bool refetch_now() {
  g_battery = read_battery();
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok);
  if (!wifi_connect()) {
    Serial.println("[main] wifi failed");
    return false;
  }
  const bool ntp_ok = sync_ntp_once();

  std::vector<PageRecord> fresh;
  const bool ok = axiom_fetch_pages(fresh) && !fresh.empty();
  if (!ok) {
    Serial.println("[main] fetch failed; keeping cache");
    return false;
  }

  g_state.pages = std::move(fresh);
  if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;

  if (ntp_ok) {
    g_state.last_fetch_s = (uint64_t)time(nullptr);
  } else {
    uint64_t max_data_ts = 0;
    for (const auto& p : g_state.pages) max_data_ts = std::max(max_data_ts, p.data_ts);
    if (max_data_ts > g_state.last_fetch_s) g_state.last_fetch_s = max_data_ts;
  }
  nvs_save(g_state);
  Serial.printf("[main] fetched %u pages idx=%u\n",
                (unsigned)g_state.pages.size(), (unsigned)g_state.page_index);
  return true;
}

void render_current() {
  g_battery = read_battery();
  if (g_state.pages.empty()) {
    render_error("no pages - fetch failed or producer empty");
    return;
  }
  if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;
  const PageRecord& rec = g_state.pages[g_state.page_index];

  LayoutNode root;
  if (!parse_page_layout(rec, root)) {
    render_error("layout_json parse failed");
    return;
  }

  const uint64_t now = (uint64_t)time(nullptr);
  const bool stale = g_state.last_fetch_s > 0 && now > g_state.last_fetch_s + FETCH_INTERVAL_SEC;
  render_page(rec, root, stale, g_state, g_battery);

  g_state.last_rendered_id = rec.id;
  g_state.last_rendered_data_ts = rec.data_ts;
  nvs_save_render_marker(g_state.last_rendered_id, g_state.last_rendered_data_ts);
}

void dump_state() {
  Serial.printf("[state] pages=%u idx=%u last_fetch=%llu heap=%u psram=%u batt=%u%%/%umV\n",
                (unsigned)g_state.pages.size(),
                (unsigned)g_state.page_index,
                (unsigned long long)g_state.last_fetch_s,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram(),
                (unsigned)g_battery.pct,
                (unsigned)g_battery.mv);
  for (size_t i = 0; i < g_state.pages.size(); ++i) {
    Serial.printf("  [%u] id=%s order=%d layout=%uB ts=%llu\n",
                  (unsigned)i,
                  g_state.pages[i].id.c_str(),
                  g_state.pages[i].order,
                  (unsigned)g_state.pages[i].layout_json.length(),
                  (unsigned long long)g_state.pages[i].data_ts);
  }
}

void next_page() {
  if (g_state.pages.empty()) {
    Serial.println("[nav] no pages");
    return;
  }
  g_state.page_index = (g_state.page_index + 1) % g_state.pages.size();
  nvs_save_progress(g_state.page_index);
  Serial.printf("[nav] next idx=%u\n", (unsigned)g_state.page_index);
  render_current();
}

void prev_page() {
  if (g_state.pages.empty()) {
    Serial.println("[nav] no pages");
    return;
  }
  g_state.page_index = (g_state.page_index + g_state.pages.size() - 1) % g_state.pages.size();
  nvs_save_progress(g_state.page_index);
  Serial.printf("[nav] prev idx=%u\n", (unsigned)g_state.page_index);
  render_current();
}

void do_refresh() {
  if (refetch_now()) render_current();
  if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
}

void console_help() {
  Serial.println("[console] f fetch, r render, n next, b prev, s state, q layout, c clear, R reboot, h help");
}

void console_dispatch(char c) {
  switch (c) {
    case 'h':
    case '?':
      console_help();
      break;
    case 'f':
      Serial.println("[console] fetch");
      do_refresh();
      break;
    case 'r':
      Serial.println("[console] render");
      render_current();
      break;
    case 'n':
      next_page();
      break;
    case 'b':
      prev_page();
      break;
    case 's':
      dump_state();
      break;
    case 'q':
      if (g_state.pages.empty()) {
        Serial.println("[console] no pages");
      } else {
        const auto& rec = g_state.pages[g_state.page_index];
        Serial.printf("[console] layout_json id=%s len=%u:\n%s\n",
                      rec.id.c_str(),
                      (unsigned)rec.layout_json.length(),
                      rec.layout_json.c_str());
      }
      break;
    case 'c':
      Serial.println("[console] clear");
      clear_screen();
      break;
    case 'R':
      Serial.println("[console] reboot");
      Serial.flush();
      delay(50);
      esp_restart();
      break;
    case '\n':
    case '\r':
    case ' ':
    case 0:
      break;
    default:
      Serial.printf("[console] unknown 0x%02x\n", (unsigned)(uint8_t)c);
      break;
  }
}

// Two physical buttons, both active-low. A short tap pages; holding either
// button past kLongPressMs triggers a refetch (the only "third gesture" two
// buttons can offer without colliding with rapid paging). GPIO0 doubles as
// BOOT, but is safe as a runtime button — it only enters download mode when
// held across a reset.
struct Button {
  int pin;
  char tap_cmd;       // console command dispatched on a short tap
  bool pressed;       // currently held down
  uint32_t press_ms;  // millis() when the press began
  bool long_fired;    // long-press refresh already fired for this press
};

Button g_buttons[] = {
  {PIN_BUTTON_1, 'b', false, 0, false},  // GPIO0  -> prev page
  {PIN_BUTTON_2, 'n', false, 0, false},  // GPIO21 -> next page
};

constexpr uint32_t kLongPressMs = 800;
constexpr uint32_t kDebounceMs = 40;

void buttons_init() {
  for (auto& b : g_buttons) {
    pinMode(b.pin, INPUT_PULLUP);
    b.pressed = digitalRead(b.pin) == 0;
    b.press_ms = millis();
    b.long_fired = false;
  }
}

void buttons_poll() {
  const uint32_t now = millis();
  for (auto& b : g_buttons) {
    const bool down = digitalRead(b.pin) == 0;
    if (down && !b.pressed) {
      // Press edge. Bounces just reset the timer until the contact settles.
      b.pressed = true;
      b.press_ms = now;
      b.long_fired = false;
    } else if (down && b.pressed) {
      if (!b.long_fired && now - b.press_ms >= kLongPressMs) {
        b.long_fired = true;
        Serial.printf("[btn] gpio%d long-press -> refresh\n", b.pin);
        do_refresh();
      }
    } else if (!down && b.pressed) {
      const uint32_t held = now - b.press_ms;
      b.pressed = false;
      if (!b.long_fired && held >= kDebounceMs) {
        Serial.printf("[btn] gpio%d tap -> %c\n", b.pin, b.tap_cmd);
        console_dispatch(b.tap_cmd);
      }
    }
  }
}

}  // namespace rcpaper

using namespace rcpaper;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] rcink t-amoled");

  apply_local_tz();
  display_init();
  buttons_init();

  g_battery = read_battery();
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok);

  g_state = nvs_load();
  if (g_state.pages.empty()) {
    Serial.println("[main] empty cache -> initial fetch");
    if (refetch_now()) {
      render_current();
    } else {
      render_error("initial fetch failed");
    }
    if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
  } else {
    render_current();
  }
  console_help();
  Serial.println("[console] ready");
}

void loop() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    console_dispatch((char)c);
  }
  buttons_poll();
  delay(20);
}
