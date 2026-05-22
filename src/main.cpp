// src/main.cpp
//
// Orchestrator. Owns the wake cycle:
//   sense → fetch when scheduled/forced → render or skip → deep sleep.
//
// Render and dev-console primitives live in their own modules:
//   render/status_bar.cpp, render/nav_strip.cpp, render/page.cpp,
//   dev/console.cpp.
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <time.h>

#include "config.h"
#include "net/axiom.h"
#include "net/wifi.h"
#include "power/nav_delta.h"
#include "power/sleep.h"
#include "proto/parser.h"
#include "render/page.h"
#include "sense/battery.h"
#include "sense/env.h"
#include "status/led_status.h"
#include "store/nvs.h"

#ifdef RCPAPER_DEV_CONSOLE
#include "dev/console.h"
#endif

namespace rcpaper {

// Persistent across the dev-console loop. Loaded once in setup().
// Non-static so dev/console.cpp can `extern` it for `s`/`n`/`b` dispatch.
DeviceState g_state;

// Transient — re-read every wake / refetch. Not persisted in NVS.
static BatteryReading g_battery{};
static EnvReading     g_env{};

// ---- nav-press coalescing ------------------------------------------------
//
// On a ButtonA/ButtonB wake we count taps for 500 ms (sliding, max 2 s) and
// apply the net delta in one render. BtnC during the window cancels nav and
// requests a refetch — see coalesce_nav_presses() below and the apply step
// further down in setup().
//
// Detection is ISR-based on the raw button GPIOs (mirroring the GPIO
// numbers used by ext1 wake in power/sleep.cpp). The ISRs are installed at
// the very top of setup() — BEFORE M5.begin(), which takes ~200 ms — so a
// second tap that arrives while the device is still booting is captured.
// The wake press itself does NOT fire the ISR (the GPIO is already LOW when
// we install attachInterrupt(FALLING)), so it is only counted via the wake
// reason. M5.BtnA/B/C are deliberately not used: dev/console.cpp also
// chose raw GPIO, and with `board = esp32s3box` in platformio.ini we cannot
// rely on M5Unified auto-detecting M5PaperColor's button map.

#define BTN_A_GPIO 9
#define BTN_B_GPIO 10
#define BTN_C_GPIO 1

constexpr uint16_t kNavCoalesceWindowMs = 500;
constexpr uint16_t kNavCoalesceMaxMs    = 2000;
constexpr uint16_t kNavPollIntervalMs   = 10;
constexpr uint16_t kNavLedPulseMs       = 80;
constexpr uint32_t kNavDebounceMs       = 30;

namespace {
volatile uint32_t g_isr_count_a    = 0;
volatile uint32_t g_isr_count_b    = 0;
volatile uint32_t g_isr_count_c    = 0;
volatile uint32_t g_isr_last_a_ms  = 0;
volatile uint32_t g_isr_last_b_ms  = 0;
volatile uint32_t g_isr_last_c_ms  = 0;
}  // namespace

void IRAM_ATTR isr_btn_a() {
  uint32_t now = millis();
  if (now - g_isr_last_a_ms < kNavDebounceMs) return;
  g_isr_last_a_ms = now;
  g_isr_count_a++;
}
void IRAM_ATTR isr_btn_b() {
  uint32_t now = millis();
  if (now - g_isr_last_b_ms < kNavDebounceMs) return;
  g_isr_last_b_ms = now;
  g_isr_count_b++;
}
void IRAM_ATTR isr_btn_c() {
  uint32_t now = millis();
  if (now - g_isr_last_c_ms < kNavDebounceMs) return;
  g_isr_last_c_ms = now;
  g_isr_count_c++;
}

static void install_button_isrs() {
  pinMode(BTN_A_GPIO, INPUT_PULLUP);
  pinMode(BTN_B_GPIO, INPUT_PULLUP);
  pinMode(BTN_C_GPIO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_A_GPIO), isr_btn_a, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_B_GPIO), isr_btn_b, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_C_GPIO), isr_btn_c, FALLING);
}

struct NavCoalesceResult {
  int  delta         = 0;
  bool force_refetch = false;
};

static void flash_for_button(WakeReason r) {
  switch (r) {
    case WakeReason::ButtonA: led_status_pulse(0,  96, 0,  kNavLedPulseMs); break;
    case WakeReason::ButtonB: led_status_pulse(0,  0,  96, kNavLedPulseMs); break;
    case WakeReason::ButtonC: led_status_pulse(96, 56, 0,  kNavLedPulseMs); break;
    default: break;
  }
}

// Caller must only invoke this for WakeReason::ButtonA or ButtonB.
static NavCoalesceResult coalesce_nav_presses(WakeReason initial) {
  NavCoalesceResult out;
  out.delta = (initial == WakeReason::ButtonA) ? 1 : -1;
  flash_for_button(initial);

  // Snapshot any boot-window presses captured by ISRs before we got here.
  uint32_t boot_a = g_isr_count_a;
  uint32_t boot_b = g_isr_count_b;
  uint32_t boot_c = g_isr_count_c;
  Serial.printf("[nav] enter initial=%d boot_isr a=%u b=%u c=%u\n",
                (int)initial, (unsigned)boot_a, (unsigned)boot_b, (unsigned)boot_c);

  // Replay boot presses as if they happened at function entry.
  out.delta += (int)boot_a;
  out.delta -= (int)boot_b;
  for (uint32_t i = 0; i < boot_a; ++i) led_status_pulse(0, 96, 0, kNavLedPulseMs);
  for (uint32_t i = 0; i < boot_b; ++i) led_status_pulse(0, 0, 96, kNavLedPulseMs);
  if (boot_c > 0) {
    out.force_refetch = true;
    led_status_pulse(96, 56, 0, kNavLedPulseMs);
    Serial.printf("[nav] coalesce done delta=%d force_refetch=1 (boot-c)\n", out.delta);
    return out;
  }

  const uint32_t start_ms = millis();
  uint32_t deadline_ms    = start_ms + kNavCoalesceWindowMs;
  uint32_t seen_a = boot_a;
  uint32_t seen_b = boot_b;
  uint32_t seen_c = boot_c;

  for (;;) {
    uint32_t now = millis();
    if ((int32_t)(deadline_ms - now) <= 0) break;
    if (now - start_ms >= kNavCoalesceMaxMs) break;

    uint32_t cur_a = g_isr_count_a;
    uint32_t cur_b = g_isr_count_b;
    uint32_t cur_c = g_isr_count_c;
    uint32_t new_a = cur_a - seen_a;
    uint32_t new_b = cur_b - seen_b;
    uint32_t new_c = cur_c - seen_c;

    if (new_a > 0) {
      out.delta += (int)new_a;
      for (uint32_t i = 0; i < new_a; ++i) led_status_pulse(0, 96, 0, kNavLedPulseMs);
      deadline_ms = now + kNavCoalesceWindowMs;
      seen_a = cur_a;
    }
    if (new_b > 0) {
      out.delta -= (int)new_b;
      for (uint32_t i = 0; i < new_b; ++i) led_status_pulse(0, 0, 96, kNavLedPulseMs);
      deadline_ms = now + kNavCoalesceWindowMs;
      seen_b = cur_b;
    }
    if (new_c > 0) {
      out.force_refetch = true;
      led_status_pulse(96, 56, 0, kNavLedPulseMs);
      seen_c = cur_c;
      break;
    }
    delay(kNavPollIntervalMs);
  }
  Serial.printf("[nav] coalesce done delta=%d force_refetch=%d window=%u ms\n",
                out.delta, (int)out.force_refetch,
                (unsigned)(millis() - start_ms));
  return out;
}

static void apply_local_tz() {
  setenv("TZ", LOCAL_TZ, 1);
  tzset();
}

// Force SNTP to actually deliver a fresh sample, not just inherit whatever the
// RTC chip holds. getLocalTime() returns success as soon as `tm_year >= 2017`,
// which is true on the first wake when the RTC chip retains stale (often
// local-time-treated-as-UTC) data from earlier flashes — so the firmware would
// silently record a wrong `time(nullptr)` into last_fetch_s and the bottom
// "refresh HH:MM" would render `+TZ` shifted from reality. Polling
// sntp_get_sync_status() until COMPLETED guarantees the SNTP daemon got a
// reply.
static bool sync_ntp_once() {
  // pool.ntp.org is often blocked on Chinese networks; try aliyun first.
  led_status_show(LedState::Syncing);
  configTime(0, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
  // Apply local TZ so subsequent localtime_r() calls (e.g. for the footer
  // HH:MM stamps) render in the user's wall-clock instead of UTC.
  apply_local_tz();
  const uint32_t timeout_ms = 10000;
  const uint32_t start = millis();
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    if (millis() - start > timeout_ms) {
      Serial.println("[ntp] sync timeout — SNTP did not deliver");
      led_status_show(LedState::SyncFailed);
      return false;
    }
    delay(100);
    led_status_tick();
  }
  time_t now;
  struct tm ti;
  time(&now);
  localtime_r(&now, &ti);
  Serial.printf("[ntp] synced in %u ms; now=%04d-%02d-%02d %02d:%02d:%02d (tz=%s)\n",
                (unsigned)(millis() - start),
                ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                ti.tm_hour, ti.tm_min, ti.tm_sec, LOCAL_TZ);
  return true;
}

// ---- shared cycle steps ---------------------------------------------------
//
// WiFi lifecycle contract: refetch_now() connects and leaves wifi up so a
// subsequent render_current() can pull remote images on the same wake.
// render_current() additionally connects on-demand when refetch didn't run
// but the page wants a remote image. The single cleanup site is the setup()
// epilogue (and the console dispatch for 'f' / 'r') — do not disconnect
// inside refetch_now() or render_current(), or remote images break.
bool refetch_now() {
  g_battery = read_battery();
  g_env     = read_sht40();
  if (!wifi_connect()) {
    Serial.println("[main] wifi failed");
    return false;
  }
  bool ntp_ok = sync_ntp_once();
  led_status_show(LedState::Syncing);
  std::vector<PageRecord> fresh;
  bool ok = axiom_fetch_pages(fresh) && !fresh.empty();
  if (ok) {
    g_state.pages = std::move(fresh);
    // Preserve g_state.page_index: scheduled refreshes update content without
    // changing pages. setup() clamps idx if the producer shrank the page list.
    // Stamp last_fetch_s: prefer NTP-synced wall clock; fall back to the max
    // data_ts across fetched pages (server-authoritative UTC) so the refresh
    // indicator always advances on a successful fetch even when NTP is down.
    if (ntp_ok) {
      g_state.last_fetch_s = (uint64_t)time(nullptr);
    } else {
      uint64_t max_data_ts = 0;
      for (const auto& p : g_state.pages) max_data_ts = std::max(max_data_ts, p.data_ts);
      if (max_data_ts > g_state.last_fetch_s) g_state.last_fetch_s = max_data_ts;
    }
    nvs_save(g_state);
    Serial.printf("[main] fetched %u pages, idx=%u (last_fetch updated=%d)\n",
                  (unsigned)g_state.pages.size(),
                  (unsigned)g_state.page_index, ntp_ok ? 1 : 0);
  } else {
    Serial.println("[main] fetch failed; keeping cache");
    led_status_show(LedState::SyncFailed);
  }
  return ok;
}

void render_current() {
  if (g_state.pages.empty()) {
    led_status_show(LedState::Refreshing);
    render_error("no pages — check producer + Axiom dataset");
    return;
  }
  const PageRecord& rec = g_state.pages[g_state.page_index];

  // Skip the panel refresh when (id, data_ts) match what we drew last time.
  // The Spectra 6 panel has no genuinely-faster refresh mode (epd_quality /
  // epd_fast / epd_fastest all clock ~16.8 s on this hardware), and the image
  // already on the panel is byte-equivalent to what we'd render — so skipping
  // is invisible to the user and saves both wall-clock + panel-wear refreshes.
  if (g_state.last_rendered_id == rec.id &&
      g_state.last_rendered_data_ts == rec.data_ts) {
    Serial.printf("[render] skip — id=%s data_ts=%llu unchanged\n",
                  rec.id.c_str(), (unsigned long long)rec.data_ts);
    return;
  }

  LayoutNode root;
  if (!parse_page_layout(rec, root)) {
    led_status_show(LedState::Refreshing);
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
  led_status_show(LedState::Refreshing);
  render_page(rec, root, stale, g_state, g_battery, g_env);

  // Persist the render marker so the next wake (cold or warm) can compare and
  // skip if nothing changed. Partial NVS write — see nvs_save_render_marker.
  g_state.last_rendered_id      = rec.id;
  g_state.last_rendered_data_ts = rec.data_ts;
  nvs_save_render_marker(g_state.last_rendered_id, g_state.last_rendered_data_ts);

  if (connected_here) {
    wifi_disconnect();
  }
}

}  // namespace rcpaper

using namespace rcpaper;

// ---- entry points ---------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(50);

  // Arm raw-GPIO button ISRs as early as possible so taps that arrive during
  // M5.begin() (~200 ms) are captured. See the comment block above the ISR
  // definitions for why this is ISR-based rather than M5.update()-based.
  install_button_isrs();

  auto cfg = M5.config();
  // clear_display would force a full ~17 s panel refresh to white at the
  // start of every wake. Skip it: render_page always fillSprite + paint over
  // the full canvas, and on wakes where render_current() decides to skip
  // (same id + data_ts as last render) the panel correctly keeps showing
  // the prior image. With clear_display=true a skipped wake left the panel
  // stranded on the M5.begin white-out.
  cfg.clear_display = false;
  cfg.output_power  = true;
  M5.begin(cfg);
  led_status_init();
  // epd_quality buffers all draws into M5GFX's offscreen buffer and commits
  // them in one full-panel refresh on Display.display(). epd_text auto-pushes
  // after each text write, which on a 6-color panel manifests as one full
  // refresh cycle per element drawn.
  M5.Display.setEpdMode(epd_mode_t::epd_quality);

  // Apply local TZ once per boot. TZ env survives within a process but not
  // across esp_restart/deep-sleep cycles, so set it before any localtime_r()
  // call (e.g. on cache-only wakes that skip NTP).
  apply_local_tz();

  g_battery = read_battery();
  g_env     = read_sht40();
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d  env=%.1fC %.0f%% ok=%d\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok,
                g_env.temp_c, g_env.humidity_pct, g_env.ok);

  WakeReason wr = current_wake_reason();
  Serial.printf("[main] wake reason = %u\n", (unsigned)wr);

  g_state = nvs_load();

  bool needs_fetch =
      g_state.pages.empty()
      || wr == WakeReason::ButtonC
      || wr == WakeReason::Timer;

  if (needs_fetch) {
    refetch_now();
  }

  // Page navigation is button-only. Timer wakes are scheduled refreshes and
  // must not advance the page; on a nav wake we coalesce rapid taps within a
  // sliding 500 ms window (cap 2 s) into a single net delta + one render.
  // BtnC pressed during coalescing cancels nav and forces a refetch.
  if (!g_state.pages.empty()) {
    uint8_t prev_page_index = g_state.page_index;
    if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;

    if (wr == WakeReason::ButtonA || wr == WakeReason::ButtonB) {
      NavCoalesceResult nav = coalesce_nav_presses(wr);
      if (nav.force_refetch) {
        refetch_now();
      }
      if (nav.delta != 0) {
        g_state.page_index = apply_nav_delta(g_state.page_index, nav.delta,
                                             g_state.pages.size());
      }
    }
    if (g_state.page_index != prev_page_index) {
      nvs_save_progress(g_state.page_index);
    }
  }

  render_current();
  if (WiFi.status() == WL_CONNECTED) {
    wifi_disconnect();
  } else {
    led_status_show(LedState::IdleDisconnected);
  }

#ifdef RCPAPER_DEV_CONSOLE
  dev::dev_buttons_init();
  led_status_show(LedState::IdleDisconnected);
  Serial.println("[console] ready — type 'h' for commands. Deep sleep disabled.");
#else
  enter_deep_sleep_until((uint64_t)time(nullptr) + FETCH_INTERVAL_SEC);
#endif
}

void loop() {
#ifdef RCPAPER_DEV_CONSOLE
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    dev::console_dispatch((char)c);
  }
  dev::dev_buttons_poll();
  delay(20);
#endif
}
