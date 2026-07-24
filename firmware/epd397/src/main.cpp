#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "display/epd397.h"
#include "display/pins_config.h"
#include "net/axiom.h"
#include "net/ntp.h"
#include "net/wifi.h"
#include "power/refresh_schedule.h"
#include "power/sleep.h"
#include "proto/parser.h"
#include "render/diagnostics.h"
#include "render/page.h"
#include "sense/battery.h"
#include "sense/rtc.h"
#include "sense/shtc3.h"
#include "store/nvs.h"

namespace rcpaper {

// The ESP32-S3-ePaper-3.97 is a desk climate display. It boots into a hand-drawn
// DASHBOARD (clock + indoor temp/humidity from the onboard SHTC3 + battery) and
// can flip to the Axiom page-DECK ported from the AMOLED target. The dashboard
// works fully offline: time comes from the PCF85063 RTC (NTP re-syncs it when
// Wi-Fi is up) and the climate from I2C.
enum class Mode : uint8_t { Dashboard, Deck };

DeviceState g_state;
BatteryReading g_battery;
Shtc3Reading g_env;
Mode g_mode = Mode::Dashboard;

static uint32_t g_next_auto_refresh_ms = 0;
RTC_DATA_ATTR static uint32_t g_wake_count = 0;
static const char* g_last_result = "boot";
// E-paper can't flash every 30 s, so the dashboard re-renders only when the
// displayed minute rolls over (≈once/min). -1 forces a render on the first tick.
static int g_last_dash_min = -1;

static_assert(FETCH_INTERVAL_SEC > 0, "FETCH_INTERVAL_SEC must be > 0");

static constexpr uint64_t kWakeButtonMask =
    (1ULL << PIN_BUTTON_UP) | (1ULL << PIN_BUTTON_2) | (1ULL << PIN_BUTTON_DOWN);

static const char* mode_name(Mode m) {
  return m == Mode::Dashboard ? "dashboard" : "deck";
}

static uint8_t mode_to_nvs(Mode m) {
  return m == Mode::Dashboard ? 0 : 1;
}

static Mode mode_from_nvs(uint8_t raw) {
  return raw == 1 ? Mode::Deck : Mode::Dashboard;
}

static bool wall_clock_is_valid() {
  return time(nullptr) >= 1704067200;  // 2024-01-01 UTC; before that means RTC/NTP absent.
}

static bool network_refresh_due() {
  const uint64_t now = (uint64_t)time(nullptr);
  const uint32_t interval = adaptive_refresh_interval_sec(
      FETCH_INTERVAL_SEC, g_state.unchanged_fetches, g_battery.pct, g_battery.ok);
  return g_state.pages.empty() || !wall_clock_is_valid() ||
         g_state.last_fetch_s == 0 || now >= g_state.last_fetch_s + interval;
}

#ifndef RCEPD397_DEV_CONSOLE
static int wake_button_pin() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return -1;
  const uint64_t mask = esp_sleep_get_ext1_wakeup_status();
  if (mask & (1ULL << PIN_BUTTON_UP)) return PIN_BUTTON_UP;
  if (mask & (1ULL << PIN_BUTTON_2)) return PIN_BUTTON_2;
  if (mask & (1ULL << PIN_BUTTON_DOWN)) return PIN_BUTTON_DOWN;
  return -1;
}

static uint64_t next_minute_epoch() {
  const uint64_t now = (uint64_t)time(nullptr);
  return ((now / 60) + 1) * 60;
}

[[noreturn]] void sleep_after_render() {
  if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
  display_sleep();
  pinMode(PIN_BUTTON_UP, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);
  pinMode(PIN_BUTTON_DOWN, INPUT_PULLUP);
  esp_sleep_enable_ext1_wakeup(kWakeButtonMask, ESP_EXT1_WAKEUP_ANY_LOW);
  deep_sleep_until(next_minute_epoch());
}
#endif

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
  const time_t now = time(nullptr);
  render_dashboard(g_env, g_battery, (uint64_t)now);
  struct tm ti;
  localtime_r(&now, &ti);
  g_last_dash_min = ti.tm_min;
}

// Connect Wi-Fi, sync NTP -> RTC, fetch the Axiom deck into the page cache.
// Returns true if the page fetch succeeded. Rendering is left to the caller.
bool refetch_now() {
  g_battery = read_battery();
  render_status("Connecting Wi-Fi...");
  if (!wifi_connect()) {
    Serial.println("[main] wifi failed");
    g_last_result = "wifi-failed";
    return false;
  }
  render_status("Syncing time...");
  if (ntp_sync_once(LOCAL_TZ)) write_rtc_from_clock();

  render_status("Fetching data...");
  std::vector<PageRecord> fresh;
  const bool ok = axiom_fetch_pages(fresh) && !fresh.empty();
  if (!ok) {
    Serial.println("[main] fetch failed; keeping cache");
    g_last_result = "fetch-failed";
    return false;
  }

  record_successful_fetch(g_state, fresh);
  g_state.pages = std::move(fresh);
  if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;
  g_state.last_fetch_s = (uint64_t)time(nullptr);
  nvs_save(g_state);
  g_last_result = "fetch-ok";
  Serial.printf("[main] fetched %u pages idx=%u\n",
                (unsigned)g_state.pages.size(), (unsigned)g_state.page_index);
  return true;
}

void render_current() {
  g_battery = read_battery();
  if (g_state.pages.empty()) {
    g_last_result = "no-pages";
    render_error("no pages - hold FN to fetch");
    return;
  }
  if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;
  const PageRecord& rec = g_state.pages[g_state.page_index];

  LayoutNode root;
  if (!parse_page_layout(rec, root)) {
    g_last_result = "parse-failed";
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

void render_diagnostics() {
  g_battery = read_battery();
  DiagnosticsSnapshot snap;
  snap.free_heap = ESP.getFreeHeap();
  snap.free_psram = ESP.getFreePsram();
  snap.battery_ok = g_battery.ok;
  snap.battery_pct = g_battery.pct;
  snap.battery_mv = g_battery.mv;
  snap.wifi_connected = WiFi.status() == WL_CONNECTED;
  snap.rssi_dbm = snap.wifi_connected ? WiFi.RSSI() : 0;
  snap.last_fetch_s = g_state.last_fetch_s;
  snap.data_fingerprint = g_state.last_fetch_fingerprint;
  snap.unchanged_fetches = g_state.unchanged_fetches;
  snap.wake_count = g_wake_count;
  snap.last_result = g_last_result;
  PageRecord rec;
  rec.id = "diagnostics";
  rec.order = -1;
  rec.data_ts = (uint64_t)time(nullptr);
  LayoutNode root = build_diagnostics_page(snap);
  render_page(rec, root, false, g_state, g_battery);
}

void render_current_mode() {
  if (g_mode == Mode::Dashboard) render_dashboard_now();
  else render_current();
}

void set_mode(Mode m) {
  g_mode = m;
  g_state.view_id = mode_to_nvs(m);
  nvs_save_view(g_state.view_id);
  Serial.printf("[mode] -> %s\n", mode_name(m));
}

void enter_mode(Mode m) {
  set_mode(m);
  render_current_mode();
}

void toggle_mode() {
  enter_mode(g_mode == Mode::Dashboard ? Mode::Deck : Mode::Dashboard);
}

void dump_state() {
  Serial.printf("[state] mode=%s pages=%u idx=%u T=%.1f RH=%.0f env_ok=%d "
                "last_fetch=%llu heap=%u psram=%u batt=%u%%/%umV\n",
                mode_name(g_mode),
                (unsigned)g_state.pages.size(), (unsigned)g_state.page_index,
                g_env.temp_c, g_env.rh, (int)g_env.ok,
                (unsigned long long)g_state.last_fetch_s,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
                (unsigned)g_battery.pct, (unsigned)g_battery.mv);
}

void dump_frame() {
  size_t len = 0;
  const uint8_t* frame = epd397_last_frame(&len);
  const char* mode = g_mode == Mode::Dashboard ? "dashboard" : "deck";
  const char* id = "dashboard";
  if (g_mode == Mode::Deck && !g_state.pages.empty() && g_state.page_index < g_state.pages.size()) {
    id = g_state.pages[g_state.page_index].id.c_str();
  }
  Serial.printf("[frame] begin w=%d h=%d bpp=2 bytes=%u mode=%s idx=%u count=%u id=%s\n",
                SCREEN_W, SCREEN_H, (unsigned)len, mode,
                (unsigned)g_state.page_index, (unsigned)g_state.pages.size(), id);
  constexpr size_t kBytesPerLine = 32;
  for (size_t i = 0; i < len; i += kBytesPerLine) {
    const size_t end = (i + kBytesPerLine < len) ? i + kBytesPerLine : len;
    for (size_t j = i; j < end; ++j) {
      if (frame[j] < 0x10) Serial.print('0');
      Serial.print(frame[j], HEX);
    }
    Serial.println();
    delay(1);
  }
  Serial.println("[frame] end");
  Serial.flush();
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
  const size_t n = g_state.pages.size();
  g_state.page_index = (g_state.page_index + n - 1) % n;
  nvs_save_progress(g_state.page_index);
  Serial.printf("[nav] prev idx=%u\n", (unsigned)g_state.page_index);
  render_current();
}

void apply_wake_button(int pin) {
  if (pin == PIN_BUTTON_2) {  // FUNCTION centre
    set_mode(g_mode == Mode::Dashboard ? Mode::Deck : Mode::Dashboard);
    return;
  }
  if (g_mode == Mode::Deck) {
    if (g_state.pages.empty()) {
      Serial.println("[wake] deck nav ignored: no pages");
      return;
    }
    if (pin == PIN_BUTTON_UP) {
      const size_t n = g_state.pages.size();
      g_state.page_index = (g_state.page_index + n - 1) % n;
      Serial.printf("[wake] gpio%d -> prev idx=%u\n", pin, (unsigned)g_state.page_index);
    } else {
      g_state.page_index = (g_state.page_index + 1) % g_state.pages.size();
      Serial.printf("[wake] gpio%d -> next idx=%u\n", pin, (unsigned)g_state.page_index);
    }
    nvs_save_progress(g_state.page_index);
    return;
  }
  Serial.printf("[wake] gpio%d -> dashboard refresh\n", pin);
}

void schedule_next_auto_refresh(uint32_t now_ms) {
  const uint32_t interval = adaptive_refresh_interval_sec(
      FETCH_INTERVAL_SEC, g_state.unchanged_fetches, g_battery.pct, g_battery.ok);
  g_next_auto_refresh_ms = schedule_next_refresh_ms(now_ms, interval);
  Serial.printf("[auto] next network refresh in %u s (unchanged=%u batt=%u%%)\n",
                (unsigned)interval, (unsigned)g_state.unchanged_fetches,
                (unsigned)g_battery.pct);
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
  const time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  if (ti.tm_min == g_last_dash_min) return;  // still the same displayed minute
  render_dashboard_now();
}

void console_help() {
  Serial.println("[console] f fetch, r render, d diagnostics, m toggle-mode, n next(deck), "
                 "t read-sensors, s state, q layout, p frame, c clear, R reboot, h help");
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
    case 'd':
      Serial.println("[console] diagnostics");
      render_diagnostics();
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
    case 'p':
      dump_frame();
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

// The 拨轮 (3-way wheel) drives the whole UI, leaving BOOT and the hardware PWR
// key free — so PWR can power the board off cleanly. All three are active-low.
//   UP   (GPIO4) tap  -> deck: previous page; dashboard: re-read sensors
//   FN   (GPIO5) tap  -> toggle dashboard <-> deck mode
//   DOWN (GPIO6) tap  -> deck: next page; dashboard: re-read sensors
//   any, hold >=800ms -> network refresh (Wi-Fi -> NTP/RTC + Axiom deck)
// Deck page turns use the fast ~1.5s mono refresh; everything else is 4-gray.
struct Button {
  int pin;
  bool pressed;
  uint32_t press_ms;
  bool long_fired;
};

Button g_buttons[] = {
    {PIN_BUTTON_UP, false, 0, false},    // GPIO4 = wheel UP
    {PIN_BUTTON_2, false, 0, false},     // GPIO5 = wheel FUNCTION (centre)
    {PIN_BUTTON_DOWN, false, 0, false},  // GPIO6 = wheel DOWN
};

constexpr uint32_t kLongPressMs = 800;
constexpr uint32_t kDebounceMs = 40;

void on_button_tap(int pin) {
  if (pin == PIN_BUTTON_2) {  // FUNCTION centre
    toggle_mode();
    return;
  }
  if (g_mode == Mode::Deck) {
    if (pin == PIN_BUTTON_UP) prev_page();
    else next_page();  // DOWN
  } else {
    render_dashboard_now();  // either wheel end re-reads sensors on the dashboard
  }
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
  Serial.println("\n[boot] rcink epd397 (Waveshare ESP32-S3-ePaper-3.97)");
  g_wake_count++;

  apply_local_tz(LOCAL_TZ);
  Wire.begin(I2C_SDA, I2C_SCL, 400000);  // shared SHTC3 + PCF85063 + AXP2101 bus
  display_init();
  buttons_init();

  rtc_begin();
  shtc3_begin();
  battery_begin();  // AXP2101 fuel gauge (battery-only; rails untouched)
  seed_clock_from_rtc();
  g_battery = read_battery();
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok);

  g_state = nvs_load();
  g_mode = mode_from_nvs(g_state.view_id);
  Serial.printf("[mode] loaded %s\n", mode_name(g_mode));

#ifdef RCEPD397_DEV_CONSOLE
  // Draw the dashboard immediately from RTC + sensors (instant, offline)...
  render_dashboard_now();
  // ...then go online to sync the clock and warm the deck cache.
  do_refresh();

  console_help();
  Serial.println("[console] ready");
#else
  const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
  const int wake_pin = wake_button_pin();
  Serial.printf("[wake] cause=%d gpio=%d\n", (int)wake, wake_pin);
  if (wake_pin >= 0) apply_wake_button(wake_pin);

  if (network_refresh_due()) {
    do_refresh();
  } else {
    render_current_mode();
  }
  sleep_after_render();
#endif
}

void loop() {
#ifdef RCEPD397_DEV_CONSOLE
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    console_dispatch((char)c);
  }
  buttons_poll();
  dash_tick_poll();
  auto_refresh_poll();
  delay(20);
#endif
}
