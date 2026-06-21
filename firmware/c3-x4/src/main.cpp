#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <time.h>

#include "config.h"
#include "display/pins_config.h"
#include "net/axiom.h"
#include "net/wifi.h"
#include "power/refresh_schedule.h"
#include "proto/parser.h"
#include "render/page.h"
#include "sense/battery.h"
#include "store/nvs.h"

namespace rcpaper {

DeviceState g_state;
BatteryReading g_battery;
static uint32_t g_next_auto_refresh_ms = 0;

static_assert(FETCH_INTERVAL_SEC > 0, "FETCH_INTERVAL_SEC must be > 0");

static void apply_local_tz() {
  setenv("TZ", LOCAL_TZ, 1);
  tzset();
}

static bool sync_ntp_once() {
  configTime(0, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
  apply_local_tz();
  const uint32_t start = millis();
  bool ok = false;
  while (millis() - start <= 10000) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      ok = true;
      break;
    }
    delay(100);
  }
  // Stop SNTP before returning. If it keeps polling its UDP socket while the
  // Axiom DNS/TLS that follows runs, lwIP's TCPIP-core-lock check trips an
  // assert (udp_new_ip_type "Required to lock TCPIP core functionality!") on
  // IDF 5.x — reliably when this attempt timed out and SNTP is still retrying.
  // Each refetch re-inits SNTP via configTime above, so stopping here is safe.
  esp_sntp_stop();
  if (!ok) {
    Serial.println("[ntp] sync timeout");
    return false;
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

void render_current();  // defined below; fetch_and_restart re-renders on failure

// Fetch the deck and, on success, save it to NVS and reboot so the page renders
// from the cache on a clean heap. The PSRAM-less C3 can't hold the 48KB compose
// sprite, the ~40KB TLS buffers and the ~38KB response at once, so the sprite is
// freed for the fetch (getString needs the contiguous room) and can't be
// reliably reclaimed afterwards (fragmentation). A reboot is the clean way back:
// boot reloads the ~18KB NVS blob and renders with the sprite freshly allocated.
// Returns only when there's no new data; the caller keeps the existing cache.
void fetch_and_restart() {
  g_battery = read_battery();
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok);
  render_status("Connecting Wi-Fi...");
  if (!wifi_connect()) {
    Serial.println("[main] wifi failed");
    render_error("Wi-Fi failed");
    return;
  }
  render_status("Syncing time...");
  const bool ntp_ok = sync_ntp_once();

  render_status("Fetching data...");
  display_release_canvas();  // free the sprite so getString's ~38KB body fits
  std::vector<PageRecord> fresh;
  const bool ok = axiom_fetch_pages(fresh) && !fresh.empty();
  if (ok) {
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
    if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
    Serial.printf("[main] fetched %u pages -> reboot to render\n",
                  (unsigned)g_state.pages.size());
    Serial.flush();
    delay(50);
    ESP.restart();  // boot renders the fresh cache on a clean, unfragmented heap
  }

  // No new data. Rebuild the sprite and restore the cached page (or an error if
  // nothing is cached). Deliberately no reboot — that would loop if Wi-Fi/Axiom
  // is down.
  Serial.println("[main] fetch failed; keeping cache");
  if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
  display_acquire_canvas();
  if (g_state.pages.empty()) render_error("fetch failed - no cache");
  else render_current();
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
  Serial.printf("[state] pages=%u idx=%u last_fetch=%llu heap=%u batt=%u%%/%umV\n",
                (unsigned)g_state.pages.size(),
                (unsigned)g_state.page_index,
                (unsigned long long)g_state.last_fetch_s,
                (unsigned)ESP.getFreeHeap(),
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
  g_next_auto_refresh_ms = schedule_next_refresh_ms(now_ms, FETCH_INTERVAL_SEC);
  Serial.printf("[auto] next refresh in %u s\n", (unsigned)FETCH_INTERVAL_SEC);
}

void do_refresh() {
  // On success this reboots (rendering the fresh cache on boot); it only returns
  // when the fetch failed, in which case we re-arm the next scheduled refresh.
  fetch_and_restart();
  schedule_next_auto_refresh(millis());
}

void auto_refresh_poll() {
  const uint32_t now = millis();
  if (!refresh_due_ms(now, g_next_auto_refresh_ms)) return;
  Serial.println("[auto] refresh interval elapsed");
  do_refresh();
}

void console_help() {
  Serial.println("[console] f fetch, r render, n next, b prev, s state, q layout, "
                 "P capture, c clear, R reboot, h help");
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
    case 'P':
      Serial.println("[console] capture framebuffer");
      dump_framebuffer();
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

// Xteink X4 buttons: six sit on two ADC ladders (GPIO1: Back/Confirm/Left/Right;
// GPIO2: Up/Down) plus an active-low power button (GPIO3). The ladder thresholds
// are lifted from the crosspoint InputManager. Idle reads high (~4095); pressing
// a button pulls its rail down to a known band. We debounce, then dispatch a
// console command on the rising edge (NONE -> button). Left/Up page back,
// Right/Down page forward, Confirm refetches, Back re-renders. The power button
// is read but unused for now (deep sleep is a later milestone).
enum X4Button : int8_t {
  X4_NONE = -1, X4_BACK = 0, X4_CONFIRM, X4_LEFT, X4_RIGHT, X4_UP, X4_DOWN
};

constexpr int kAdcNoButton = 3900;
constexpr uint32_t kDebounceMs = 30;

static int adc_ladder(int adc, const int* ranges, int n) {
  for (int i = 0; i < n; ++i)
    if (ranges[i + 1] < adc && adc <= ranges[i]) return i;
  return X4_NONE;
}

static int read_button() {
  static const int kR1[] = {kAdcNoButton, 3100, 2090, 750, INT32_MIN};  // Back/Confirm/Left/Right
  static const int kR2[] = {kAdcNoButton, 1120, INT32_MIN};             // Up/Down
  const int b1 = adc_ladder(analogRead(PIN_BTN_ADC_1), kR1, 4);
  if (b1 != X4_NONE) return b1;                 // 0..3
  const int b2 = adc_ladder(analogRead(PIN_BTN_ADC_2), kR2, 2);
  if (b2 != X4_NONE) return X4_UP + b2;         // 4..5
  return X4_NONE;
}

void buttons_init() {
  pinMode(PIN_BTN_ADC_1, INPUT);
  pinMode(PIN_BTN_ADC_2, INPUT);
  pinMode(PIN_BTN_POWER, INPUT_PULLUP);
}

static void on_button_press(int b) {
  switch (b) {
    case X4_LEFT:
    case X4_UP:      Serial.println("[btn] prev");    console_dispatch('b'); break;
    case X4_RIGHT:
    case X4_DOWN:    Serial.println("[btn] next");    console_dispatch('n'); break;
    case X4_CONFIRM: Serial.println("[btn] refetch"); console_dispatch('f'); break;
    case X4_BACK:    Serial.println("[btn] render");  console_dispatch('r'); break;
    default: break;
  }
}

void buttons_poll() {
  static int stable = X4_NONE;
  static int cand = X4_NONE;
  static uint32_t cand_ms = 0;
  const int raw = read_button();
  const uint32_t now = millis();
  if (raw != cand) {  // a new reading; restart the debounce window
    cand = raw;
    cand_ms = now;
    return;
  }
  if (now - cand_ms < kDebounceMs || cand == stable) return;
  const int prev = stable;
  stable = cand;
  if (prev == X4_NONE && stable != X4_NONE) on_button_press(stable);
}

}  // namespace rcpaper

using namespace rcpaper;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] rcink c3-x4 (Xteink X4)");

  apply_local_tz();
  display_init();
  buttons_init();

  g_battery = read_battery();
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok);

  g_state = nvs_load();
  if (g_state.pages.empty()) {
    // No cache: fetch, save, and reboot to render (fetch_and_restart only
    // returns if the fetch failed, leaving an error on screen).
    Serial.println("[main] empty cache -> initial fetch");
    fetch_and_restart();
  } else {
    render_current();
  }
  console_help();
  schedule_next_auto_refresh(millis());
  Serial.println("[console] ready");
}

void loop() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    console_dispatch((char)c);
  }
  buttons_poll();
  auto_refresh_poll();
  delay(20);
}
