#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <time.h>

#include "config.h"
#include "display/pins_config.h"
#include "net/axiom.h"
#include "net/ntp.h"
#include "net/wifi.h"
#include "power/refresh_schedule.h"
#include "power/sleep.h"
#include "power/wake_display_policy.h"
#include "proto/parser.h"
#include "render/diagnostics.h"
#include "render/page.h"
#include "sense/battery.h"
#include "store/nvs.h"

namespace rcpaper {

DeviceState g_state;
BatteryReading g_battery;
static uint32_t g_next_auto_refresh_ms = 0;
RTC_DATA_ATTR static uint32_t g_wake_count = 0;
static const char* g_last_result = "boot";

static_assert(FETCH_INTERVAL_SEC > 0, "FETCH_INTERVAL_SEC must be > 0");

#ifndef RCAMOLED_DEV_CONSOLE
static constexpr uint64_t kWakeButtonMask = 1ULL << PIN_BUTTON_2;  // GPIO21 only; never GPIO0/BOOT.

static bool woke_from_next_button() {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 &&
         (esp_sleep_get_ext1_wakeup_status() & kWakeButtonMask) != 0;
}

static SleepWakePolicy wait_for_button_wake_display() {
  Serial.printf("[wake] keeping next page visible for %u ms\n",
                (unsigned)kWakeDisplayVisibleMs);
  WakeDisplayState state = begin_wake_display(millis());
  while (true) {
    const WakeDisplayAction action =
        advance_wake_display(state, millis(),
                             digitalRead(PIN_BUTTON_2) == HIGH);
    if (action == WakeDisplayAction::SleepWithButtonWake) {
      return SleepWakePolicy::TimerAndButton;
    }
    if (action == WakeDisplayAction::SleepTimerOnly) {
      Serial.printf("[sleep] gpio%d held for %u ms; using timer wake only\n",
                    PIN_BUTTON_2, (unsigned)kWakeButtonHeldLimitMs);
      return SleepWakePolicy::TimerOnly;
    }
    delay(10);
  }
}

[[noreturn]] void sleep_after_render(SleepWakePolicy wake_policy) {
  if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
  display_sleep();  // AMOLED panel must be dark between production wakes.
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);
  // EXT1 is level-triggered. Re-arming it while the wake button is still held
  // would immediately boot again and skip multiple pages. Fall back to the
  // timer for this sleep; the next boot re-arms GPIO21 after it is released.
  if (wake_policy == SleepWakePolicy::TimerAndButton &&
      digitalRead(PIN_BUTTON_2) == HIGH) {
    esp_sleep_enable_ext1_wakeup(kWakeButtonMask, ESP_EXT1_WAKEUP_ANY_LOW);
  } else if (wake_policy == SleepWakePolicy::TimerAndButton) {
    Serial.printf("[sleep] gpio%d still low; button wake not armed\n",
                  PIN_BUTTON_2);
  }
  const uint32_t interval = adaptive_refresh_interval_sec(
      FETCH_INTERVAL_SEC, g_state.unchanged_fetches, g_battery.pct, g_battery.ok);
  deep_sleep_until((uint64_t)time(nullptr) + interval);
}
#endif

bool refetch_now() {
  g_battery = read_battery();
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok);
  render_status("Connecting Wi-Fi...");
  if (!wifi_connect()) {
    Serial.println("[main] wifi failed");
    g_last_result = "wifi-failed";
    render_error("Wi-Fi failed");
    return false;
  }
  render_status("Syncing time...");
  const bool ntp_ok = ntp_sync_once(LOCAL_TZ);

  render_status("Fetching data...");
  std::vector<PageRecord> fresh;
  const bool ok = axiom_fetch_pages(fresh) && !fresh.empty();
  if (!ok) {
    Serial.println("[main] fetch failed; keeping cache");
    g_last_result = "fetch-failed";
    render_error(g_state.pages.empty() ? "fetch failed - no cache" : "fetch failed - cache kept");
    return false;
  }

  record_successful_fetch(g_state, fresh);
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
  g_last_result = "fetch-ok";
  Serial.printf("[main] fetched %u pages idx=%u\n",
                (unsigned)g_state.pages.size(), (unsigned)g_state.page_index);
  return true;
}

void render_current() {
  g_battery = read_battery();
  if (g_state.pages.empty()) {
    g_last_result = "no-pages";
    render_error("no pages - fetch failed or producer empty");
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

void schedule_next_auto_refresh(uint32_t now_ms) {
  const uint32_t interval = adaptive_refresh_interval_sec(
      FETCH_INTERVAL_SEC, g_state.unchanged_fetches, g_battery.pct, g_battery.ok);
  g_next_auto_refresh_ms = schedule_next_refresh_ms(now_ms, interval);
  Serial.printf("[auto] next refresh in %u s (unchanged=%u batt=%u%%)\n",
                (unsigned)interval, (unsigned)g_state.unchanged_fetches,
                (unsigned)g_battery.pct);
}

void do_refresh() {
  if (refetch_now()) render_current();
  if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
  schedule_next_auto_refresh(millis());
}

void auto_refresh_poll() {
  const uint32_t now = millis();
  if (!refresh_due_ms(now, g_next_auto_refresh_ms)) return;
  Serial.println("[auto] refresh interval elapsed");
  do_refresh();
}

void console_help() {
  Serial.println("[console] f fetch, r render, d diagnostics, n next, b prev, s state, q layout, c clear, R reboot, h help");
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
    case 'd':
      Serial.println("[console] diagnostics");
      render_diagnostics();
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
  {PIN_BUTTON_1, 'n', false, 0, false},  // GPIO0  = lower button -> next page
  {PIN_BUTTON_2, 'b', false, 0, false},  // GPIO21 = upper button -> prev page
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
  g_wake_count++;

  apply_local_tz(LOCAL_TZ);
  display_init();
  buttons_init();

  g_battery = read_battery();
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok);

  g_state = nvs_load();
#ifdef RCAMOLED_DEV_CONSOLE
  const bool timer_wake = false;
  const bool next_wake = false;
#else
  const bool timer_wake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  const bool next_wake = woke_from_next_button();
#endif
  if (timer_wake) {
    Serial.println("[wake] timer -> refresh");
    do_refresh();
  } else if (g_state.pages.empty()) {
    Serial.println("[main] empty cache -> initial fetch");
    if (refetch_now()) {
      render_current();
    } else {
      render_error("initial fetch failed");
    }
    if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
  } else {
    if (next_wake) {
      g_state.page_index = (g_state.page_index + 1) % g_state.pages.size();
      nvs_save_progress(g_state.page_index);
      Serial.printf("[wake] gpio%d -> next idx=%u\n",
                    PIN_BUTTON_2, (unsigned)g_state.page_index);
    }
    render_current();
  }

#ifdef RCAMOLED_DEV_CONSOLE
  console_help();
  schedule_next_auto_refresh(millis());
  Serial.println("[console] ready");
#else
  SleepWakePolicy wake_policy = SleepWakePolicy::TimerAndButton;
  if (next_wake) wake_policy = wait_for_button_wake_display();
  sleep_after_render(wake_policy);
#endif
}

void loop() {
#ifdef RCAMOLED_DEV_CONSOLE
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    console_dispatch((char)c);
  }
  buttons_poll();
  auto_refresh_poll();
  delay(20);
#endif
}
