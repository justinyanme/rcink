#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "display/pins_config.h"
#include "net/axiom.h"
#include "net/wifi.h"
#include "power/refresh_schedule.h"
#include "proto/parser.h"
#include "render/page.h"
#include "sense/battery.h"
#include "sense/rtc.h"
#include "sense/shtc3.h"
#include "store/nvs.h"

namespace rcpaper {

// The RLCD-4.2 is a desk climate display. It boots into a hand-drawn DASHBOARD
// (clock + indoor temp/humidity from the onboard SHTC3 + battery) and can flip
// to the Axiom page-DECK ported from the AMOLED target. The dashboard works
// fully offline: time comes from the PCF85063 RTC (NTP re-syncs it when Wi-Fi
// is up) and the climate from I2C.
enum class Mode : uint8_t { Dashboard, Deck };

DeviceState g_state;
BatteryReading g_battery;
Shtc3Reading g_env;
Mode g_mode = Mode::Dashboard;

static uint32_t g_next_auto_refresh_ms = 0;
static uint32_t g_next_dash_ms = 0;
constexpr uint32_t kDashRefreshMs = 30000;  // re-read sensors + clock every 30 s

static_assert(FETCH_INTERVAL_SEC > 0, "FETCH_INTERVAL_SEC must be > 0");

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

// Seed the system clock from the battery-backed RTC so the dashboard shows a
// correct time immediately at boot, before (or without) any Wi-Fi/NTP.
static void seed_clock_from_rtc() {
  RtcTime rt;
  if (!rtc_get(rt) || !rt.ok || rt.year < 2024) {
    Serial.println("[rtc] no valid time (needs NTP sync)");
    return;
  }
  struct tm tmv = {};
  tmv.tm_year = rt.year - 1900;
  tmv.tm_mon = rt.mon - 1;
  tmv.tm_mday = rt.day;
  tmv.tm_hour = rt.hour;
  tmv.tm_min = rt.min;
  tmv.tm_sec = rt.sec;
  tmv.tm_isdst = -1;
  const time_t epoch = mktime(&tmv);  // interprets tmv as local (TZ already set)
  const struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
  settimeofday(&tv, nullptr);
  Serial.printf("[rtc] clock seeded %04d-%02d-%02d %02d:%02d:%02d\n",
                rt.year, rt.mon, rt.day, rt.hour, rt.min, rt.sec);
}

// Persist the NTP-synced wall clock back into the RTC for offline use.
static void write_rtc_from_clock() {
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  if (rtc_set(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec)) {
    Serial.println("[rtc] written from NTP-synced clock");
  }
}

void render_dashboard_now() {
  g_env = shtc3_read();
  g_battery = read_battery();
  Serial.printf("[dash] T=%.1fC RH=%.0f%% env_ok=%d batt=%u%%/%umV\n",
                g_env.temp_c, g_env.rh, (int)g_env.ok,
                (unsigned)g_battery.pct, (unsigned)g_battery.mv);
  render_dashboard(g_env, g_battery, (uint64_t)time(nullptr));
  g_next_dash_ms = millis() + kDashRefreshMs;
}

// Connect Wi-Fi, sync NTP -> RTC, fetch the Axiom deck into the page cache.
// Returns true if the page fetch succeeded. Rendering is left to the caller.
bool refetch_now() {
  g_battery = read_battery();
  render_status("Connecting Wi-Fi...");
  if (!wifi_connect()) {
    Serial.println("[main] wifi failed");
    return false;
  }
  render_status("Syncing time...");
  if (sync_ntp_once()) write_rtc_from_clock();

  render_status("Fetching data...");
  std::vector<PageRecord> fresh;
  const bool ok = axiom_fetch_pages(fresh) && !fresh.empty();
  if (!ok) {
    Serial.println("[main] fetch failed; keeping cache");
    return false;
  }

  g_state.pages = std::move(fresh);
  if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;
  g_state.last_fetch_s = (uint64_t)time(nullptr);
  nvs_save(g_state);
  Serial.printf("[main] fetched %u pages idx=%u\n",
                (unsigned)g_state.pages.size(), (unsigned)g_state.page_index);
  return true;
}

void render_current() {
  g_battery = read_battery();
  if (g_state.pages.empty()) {
    render_error("no pages - long-press to fetch");
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

void render_current_mode() {
  if (g_mode == Mode::Dashboard) render_dashboard_now();
  else render_current();
}

void enter_mode(Mode m) {
  g_mode = m;
  Serial.printf("[mode] -> %s\n", m == Mode::Dashboard ? "dashboard" : "deck");
  render_current_mode();
}

void toggle_mode() {
  enter_mode(g_mode == Mode::Dashboard ? Mode::Deck : Mode::Dashboard);
}

void dump_state() {
  Serial.printf("[state] mode=%s pages=%u idx=%u T=%.1f RH=%.0f env_ok=%d "
                "last_fetch=%llu heap=%u psram=%u batt=%u%%/%umV\n",
                g_mode == Mode::Dashboard ? "dash" : "deck",
                (unsigned)g_state.pages.size(), (unsigned)g_state.page_index,
                g_env.temp_c, g_env.rh, (int)g_env.ok,
                (unsigned long long)g_state.last_fetch_s,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
                (unsigned)g_battery.pct, (unsigned)g_battery.mv);
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

void schedule_next_auto_refresh(uint32_t now_ms) {
  g_next_auto_refresh_ms = schedule_next_refresh_ms(now_ms, FETCH_INTERVAL_SEC);
  Serial.printf("[auto] next network refresh in %u s\n", (unsigned)FETCH_INTERVAL_SEC);
}

void do_refresh() {
  refetch_now();
  if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
  render_current_mode();  // resilient: dashboard still draws even if fetch failed
  schedule_next_auto_refresh(millis());
}

void auto_refresh_poll() {
  const uint32_t now = millis();
  if (!refresh_due_ms(now, g_next_auto_refresh_ms)) return;
  Serial.println("[auto] network refresh interval elapsed");
  do_refresh();
}

void dash_tick_poll() {
  if (g_mode != Mode::Dashboard) return;
  if ((int32_t)(millis() - g_next_dash_ms) < 0) return;
  render_dashboard_now();
}

void console_help() {
  Serial.println("[console] f fetch, r render, m toggle-mode, n next(deck), "
                 "t read-sensors, s state, q layout, c clear, R reboot, h help");
}

void console_dispatch(char c) {
  switch (c) {
    case 'h':
    case '?':
      console_help();
      break;
    case 'f':
      Serial.println("[console] network refresh");
      do_refresh();
      break;
    case 'r':
      Serial.println("[console] render");
      render_current_mode();
      break;
    case 'm':
      toggle_mode();
      break;
    case 'n':
      if (g_mode == Mode::Deck) next_page();
      else Serial.println("[console] n: deck mode only");
      break;
    case 't':
      render_dashboard_now();
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
                      rec.id.c_str(), (unsigned)rec.layout_json.length(),
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

// Two side buttons, both active-low.
//   KEY  (GPIO18) tap  -> toggle dashboard <-> deck mode
//   BOOT (GPIO0)  tap  -> dashboard: re-read sensors; deck: next page (wraps)
//   either, hold       -> network refresh (Wi-Fi -> NTP/RTC + Axiom deck)
// BOOT doubles as the download strap but is safe at runtime (download only
// engages when it is held across a reset).
struct Button {
  int pin;
  bool pressed;
  uint32_t press_ms;
  bool long_fired;
};

Button g_buttons[] = {
    {PIN_BUTTON_1, false, 0, false},  // GPIO0  = BOOT
    {PIN_BUTTON_2, false, 0, false},  // GPIO18 = KEY
};

constexpr uint32_t kLongPressMs = 800;
constexpr uint32_t kDebounceMs = 40;

void on_button_tap(int pin) {
  if (pin == PIN_BUTTON_2) {  // KEY
    toggle_mode();
    return;
  }
  // BOOT
  if (g_mode == Mode::Deck) next_page();
  else render_dashboard_now();
}

void on_button_long(int pin) {
  Serial.printf("[btn] gpio%d long-press -> network refresh\n", pin);
  do_refresh();
}

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
      b.pressed = true;
      b.press_ms = now;
      b.long_fired = false;
    } else if (down && b.pressed) {
      if (!b.long_fired && now - b.press_ms >= kLongPressMs) {
        b.long_fired = true;
        on_button_long(b.pin);
      }
    } else if (!down && b.pressed) {
      const uint32_t held = now - b.press_ms;
      b.pressed = false;
      if (!b.long_fired && held >= kDebounceMs) {
        Serial.printf("[btn] gpio%d tap\n", b.pin);
        on_button_tap(b.pin);
      }
    }
  }
}

}  // namespace rcpaper

using namespace rcpaper;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] rcink rlcd (Waveshare ESP32-S3-RLCD-4.2)");

  apply_local_tz();
  Wire.begin(I2C_SDA, I2C_SCL, 400000);  // shared SHTC3 + PCF85063 bus
  display_init();
  buttons_init();

  rtc_begin();
  shtc3_begin();
  seed_clock_from_rtc();
  g_battery = read_battery();
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok);

  g_state = nvs_load();

  // Draw the dashboard immediately from RTC + sensors (instant, offline)...
  render_dashboard_now();
  // ...then go online to sync the clock and warm the deck cache.
  do_refresh();

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
  dash_tick_poll();
  auto_refresh_poll();
  delay(20);
}
