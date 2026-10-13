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
#include "input/button_capture.h"
#include "net/axiom.h"
#include "net/wifi.h"
#include "power/nav_delta.h"
#include "power/refresh_schedule.h"
#include "power/sleep.h"
#include "power/runtime.h"
#include "proto/parser.h"
#include "render/diagnostics.h"
#include "render/page.h"
#include "sense/battery.h"
#include "sense/env.h"
#include "status/led_status.h"
#include "store/nvs.h"


namespace rcpaper {

// Persistent across the dev-console loop. Loaded once in setup().
// Non-static so dev/console.cpp can `extern` it for `s`/`n`/`b` dispatch.
DeviceState g_state;

// Transient — re-read every wake / refetch. Not persisted in NVS.
static BatteryReading g_battery{};
static EnvReading     g_env{};
RTC_DATA_ATTR static uint32_t g_wake_count = 0;
static const char* g_last_result = "boot";

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

constexpr uint16_t kNavCoalesceWindowMs = 500;
constexpr uint16_t kNavCoalesceMaxMs    = 2000;
constexpr uint16_t kNavPollIntervalMs   = 10;
constexpr uint16_t kNavLedPulseMs       = 80;

struct NavCoalesceResult {
  int  delta         = 0;
  bool force_refetch = false;
  bool show_diagnostics = false;
};

static void flash_for_button(WakeReason r) {
  switch (r) {
    case WakeReason::ButtonA: led_status_pulse(0,  96, 0,  kNavLedPulseMs); break;
    case WakeReason::ButtonB: led_status_pulse(0,  0,  96, kNavLedPulseMs); break;
    case WakeReason::ButtonC: led_status_pulse(96, 56, 0,  kNavLedPulseMs); break;
    default: break;
  }
}

// Shared sliding-window loop driven by ISR counters. Caller seeds the initial
// delta (+1 / -1 from whichever press triggered entry) and provides baselines
// for the ISR counters so already-counted presses aren't replayed.
static NavCoalesceResult run_coalesce_loop(int initial_delta,
                                            ButtonPressCounts seen) {
  NavCoalesceResult out;
  out.delta = initial_delta;
  bool saw_a = initial_delta > 0;
  bool saw_b = initial_delta < 0;
  const uint32_t start_ms = millis();
  uint32_t deadline_ms    = start_ms + kNavCoalesceWindowMs;

  for (;;) {
    uint32_t now = millis();
    if ((int32_t)(deadline_ms - now) <= 0) break;
    if (now - start_ms >= kNavCoalesceMaxMs) break;

    const ButtonPressCounts counts = button_press_counts();
    const uint32_t new_a = counts.a - seen.a;
    const uint32_t new_b = counts.b - seen.b;
    const uint32_t new_c = counts.c - seen.c;

    if (new_a > 0) {
      saw_a = true;
      out.delta += (int)new_a;
      for (uint32_t i = 0; i < new_a; ++i) led_status_pulse(0, 96, 0, kNavLedPulseMs);
      deadline_ms = now + kNavCoalesceWindowMs;
      seen.a = counts.a;
    }
    if (new_b > 0) {
      saw_b = true;
      out.delta -= (int)new_b;
      for (uint32_t i = 0; i < new_b; ++i) led_status_pulse(0, 0, 96, kNavLedPulseMs);
      deadline_ms = now + kNavCoalesceWindowMs;
      seen.b = counts.b;
    }
    if (saw_a && saw_b) {
      out.delta = 0;
      out.show_diagnostics = true;
      Serial.println("[nav] A+B chord -> diagnostics");
      break;
    }
    if (new_c > 0) {
      out.force_refetch = true;
      led_status_pulse(96, 56, 0, kNavLedPulseMs);
      seen.c = counts.c;
      break;
    }
    delay(kNavPollIntervalMs);
  }
  Serial.printf("[nav] coalesce done delta=%d force_refetch=%d diagnostics=%d window=%u ms\n",
                out.delta, (int)out.force_refetch, (int)out.show_diagnostics,
                (unsigned)(millis() - start_ms));
  return out;
}

// Entry from setup() on a deep-sleep ButtonA/ButtonB wake. The wake press
// did NOT increment any ISR (the GPIO was already LOW when ISRs were armed),
// so we seed with ±1 from the wake reason and "replay" any ISR ticks that
// landed during M5.begin() as if they were the next presses in the window.
static NavCoalesceResult coalesce_nav_presses(WakeReason initial) {
  int delta0 = (initial == WakeReason::ButtonA) ? 1 : -1;
  flash_for_button(initial);

  const ButtonPressCounts boot_counts = button_press_counts();
  Serial.printf("[nav] enter initial=%d boot_isr a=%u b=%u c=%u\n",
                (int)initial, (unsigned)boot_counts.a, (unsigned)boot_counts.b,
                (unsigned)boot_counts.c);

  delta0 += (int)boot_counts.a - (int)boot_counts.b;
  for (uint32_t i = 0; i < boot_counts.a; ++i) {
    led_status_pulse(0, 96, 0, kNavLedPulseMs);
  }
  for (uint32_t i = 0; i < boot_counts.b; ++i) {
    led_status_pulse(0, 0, 96, kNavLedPulseMs);
  }
  if ((initial == WakeReason::ButtonA && boot_counts.b > 0) ||
      (initial == WakeReason::ButtonB && boot_counts.a > 0) ||
      (boot_counts.a > 0 && boot_counts.b > 0)) {
    NavCoalesceResult r;
    r.show_diagnostics = true;
    Serial.println("[nav] A+B chord -> diagnostics (boot)");
    return r;
  }
  if (boot_counts.c > 0) {
    NavCoalesceResult r;
    r.delta = delta0;
    r.force_refetch = true;
    led_status_pulse(96, 56, 0, kNavLedPulseMs);
    Serial.printf("[nav] coalesce done delta=%d force_refetch=1 (boot-c)\n", r.delta);
    return r;
  }
  return run_coalesce_loop(delta0, boot_counts);
}

// Entry from the dev-console button poll. Unlike the wake path, the press
// that triggered entry HAS already incremented the ISR (the falling edge
// fired both the poll and the ISR), so the current counters become the
// baseline — additional presses during the window are everything beyond it.
// Exposed (non-static) so dev/console.cpp can call it.
NavCoalesceResult coalesce_nav_from_poll(int initial_delta) {
  if (wake_reason_from_ext1_mask(sample_button_low_mask()) == WakeReason::ButtonAB) {
    NavCoalesceResult result;
    result.show_diagnostics = true;
    return result;
  }
  WakeReason fake = (initial_delta > 0) ? WakeReason::ButtonA : WakeReason::ButtonB;
  flash_for_button(fake);
  // Small delay so a pending ISR for the just-detected falling edge has
  // definitely fired before we snapshot baselines.
  delay(2);
  const ButtonPressCounts counts = button_press_counts();
  Serial.printf("[nav] poll-entry initial=%d baseline a=%u b=%u c=%u\n",
                initial_delta, (unsigned)counts.a, (unsigned)counts.b,
                (unsigned)counts.c);
  return run_coalesce_loop(initial_delta, counts);
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
    g_last_result = "wifi-failed";
    schedule_runtime_refresh();
    return false;
  }
  bool ntp_ok = sync_ntp_once();
  led_status_show(LedState::Syncing);
  std::vector<PageRecord> fresh;
  bool ok = axiom_fetch_pages(fresh) && !fresh.empty();
  if (ok) {
    record_successful_fetch(g_state, fresh);
    g_state.pages = std::move(fresh);
    if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;
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
    g_last_result = "fetch-ok";
    Serial.printf("[main] fetched %u pages, idx=%u (last_fetch updated=%d)\n",
                  (unsigned)g_state.pages.size(),
                  (unsigned)g_state.page_index, ntp_ok ? 1 : 0);
  } else {
    Serial.println("[main] fetch failed; keeping cache");
    g_last_result = "fetch-failed";
    led_status_show(LedState::SyncFailed);
  }
  schedule_runtime_refresh();
  return ok;
}

void render_current() {
  if (g_state.pages.empty()) {
    g_last_result = "no-pages";
    led_status_show(LedState::Refreshing);
    render_error("no pages — check producer + Axiom dataset");
    return;
  }
  const PageRecord& rec = g_state.pages[g_state.page_index];

  // Skip the panel refresh when (id, data_ts) match what we drew last time.
  // The pinned ED2208 driver's modes change dithering, not its waveform
  // (quality / fast / fastest all clock ~16.8 s here), and the image
  // already on the panel is byte-equivalent to what we'd render — so skipping
  // is invisible to the user and saves both wall-clock + panel-wear refreshes.
  if (render_marker_matches(g_state, rec)) {
    Serial.printf("[render] skip — id=%s data_ts=%llu unchanged\n",
                  rec.id.c_str(), (unsigned long long)rec.data_ts);
    return;
  }

  LayoutNode root;
  if (!parse_page_layout(rec, root)) {
    g_last_result = "parse-failed";
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
  // skip if nothing changed. nvs_record_rendered_page performs a tiny partial
  // write instead of re-serializing the cached pages.
  nvs_record_rendered_page(g_state, rec);

  if (connected_here) {
    wifi_disconnect();
  }
}

void render_diagnostics() {
  g_battery = read_battery();
  g_env = read_sht40();
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
  led_status_show(LedState::Refreshing);
  render_page(rec, root, false, g_state, g_battery, g_env);
  // Diagnostics replaces the physical panel too. Persist that fact; otherwise
  // the next normal wake can match a stale normal-page marker, skip its paint,
  // and leave diagnostics stranded on glass.
  nvs_record_rendered_page(g_state, rec);
}

}  // namespace rcpaper

using namespace rcpaper;

// ---- entry points ---------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(50);
  g_wake_count++;

  // Arm raw-GPIO button ISRs as early as possible so taps that arrive during
  // M5.begin() (~200 ms) are captured. See the comment block above the ISR
  // definitions for why this is ISR-based rather than M5.update()-based.
  install_button_isrs();
  // EXT1 ANY_LOW may latch only the first pin in a human A+B chord. Snapshot
  // live levels before M5.begin() while both buttons are still held.
  const uint64_t boot_button_low_mask = sample_button_low_mask();

  auto cfg = M5.config();
  // clear_display would force a full ~17 s panel refresh to white at the
  // start of every wake. Skip it: render_page always fillSprite + paint over
  // the full canvas, and on wakes where render_current() decides to skip
  // (same id + data_ts as last render) the panel correctly keeps showing
  // the prior image. With clear_display=true a skipped wake left the panel
  // stranded on the M5.begin white-out.
  cfg.clear_display = false;
  cfg.output_power  = false;
  cfg.internal_imu  = false;
  cfg.internal_spk  = false;
  cfg.internal_mic  = false;
  cfg.fallback_board = m5::board_t::board_M5PaperColor;
  M5.begin(cfg);
  if (M5.getBoard() != m5::board_t::board_M5PaperColor
      || M5.Display.width() == 0 || M5.Display.height() == 0) {
    Serial.println("[boot] fatal: PaperColor board/display initialization failed");
    Serial.flush();
    abort();
  }
  Serial.printf("[boot] PaperColor %dx%d build=%s %s\n",
                M5.Display.width(), M5.Display.height(), __DATE__, __TIME__);
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

  WakeReason wr = current_wake_reason(boot_button_low_mask);
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
  bool diagnostics_requested = wr == WakeReason::ButtonAB;
  if (!g_state.pages.empty()) {
    uint8_t prev_page_index = g_state.page_index;
    if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;

    if (wr == WakeReason::ButtonA || wr == WakeReason::ButtonB) {
      NavCoalesceResult nav = coalesce_nav_presses(wr);
      if (nav.show_diagnostics) {
        diagnostics_requested = true;
      } else if (nav.force_refetch) {
        refetch_now();
      }
      if (!nav.show_diagnostics && nav.delta != 0) {
        g_state.page_index = apply_nav_delta(g_state.page_index, nav.delta,
                                             g_state.pages.size());
      }
    }
    if (g_state.page_index != prev_page_index) {
      nvs_save_progress(g_state.page_index);
    }
  }

  if (diagnostics_requested) render_diagnostics();
  else render_current();
  if (WiFi.status() == WL_CONNECTED) {
    wifi_disconnect();
  } else {
    led_status_show(LedState::IdleDisconnected);
  }

  runtime_begin();
}

void loop() {
  runtime_poll();
}
