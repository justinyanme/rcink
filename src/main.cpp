// src/main.cpp
//
// Orchestrator. Owns the wake cycle:
//   sense → wifi → ntp → axiom fetch → render → deep sleep.
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
#include "power/sleep.h"
#include "proto/parser.h"
#include "render/page.h"
#include "sense/battery.h"
#include "sense/env.h"
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
  configTime(0, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
  // Apply local TZ so subsequent localtime_r() calls (e.g. for the footer
  // HH:MM stamps) render in the user's wall-clock instead of UTC.
  apply_local_tz();
  const uint32_t timeout_ms = 10000;
  const uint32_t start = millis();
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    if (millis() - start > timeout_ms) {
      Serial.println("[ntp] sync timeout — SNTP did not deliver");
      return false;
    }
    delay(100);
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
  std::vector<PageRecord> fresh;
  bool ok = axiom_fetch_pages(fresh) && !fresh.empty();
  if (ok) {
    g_state.pages = std::move(fresh);
    // Preserve g_state.page_index: rotation continues across fetches. The
    // page-rotate logic in setup() clamps idx if pages shrank.
    // Only stamp last_fetch_s when NTP genuinely synced — otherwise time() is
    // RTC fallback (often stale or local-as-UTC) and would render wrong.
    if (ntp_ok) g_state.last_fetch_s = (uint64_t)time(nullptr);
    nvs_save(g_state);
    Serial.printf("[main] fetched %u pages, idx=%u (last_fetch updated=%d)\n",
                  (unsigned)g_state.pages.size(),
                  (unsigned)g_state.page_index, ntp_ok ? 1 : 0);
  } else {
    Serial.println("[main] fetch failed; keeping cache");
  }
  return ok;
}

void render_current() {
  if (g_state.pages.empty()) {
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

  // Fetch cadence is gated on a deep-sleep-persistent wake counter, NOT on
  // wall clock. time(nullptr) isn't reliably restored across wakes (M5.begin
  // doesn't sync the BM8563 RTC chip into the system clock on this board),
  // so a (now - last_fetch_s) comparison either underflows or overflows and
  // fires every wake — wasting WiFi every 5 min instead of every 15.
  constexpr uint16_t WAKES_PER_FETCH = FETCH_INTERVAL_SEC / ROTATE_INTERVAL_SEC;
  bool needs_fetch =
      g_state.pages.empty()
      || wr == WakeReason::ButtonC
      || g_state.wakes_since_fetch + 1 >= WAKES_PER_FETCH;

  if (needs_fetch) {
    if (refetch_now()) {
      g_state.wakes_since_fetch = 0;
    } else {
      g_state.wakes_since_fetch++;  // still counts as a wake even if fetch failed
    }
  } else {
    g_state.wakes_since_fetch++;
  }

  // Rotate based on wake reason. refetch_now() no longer resets idx, so
  // rotation continues from where the last session left off — pages cycle
  // visibly instead of getting pinned to the post-refetch idx=0.
  if (!g_state.pages.empty()) {
    if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;
    if (wr == WakeReason::ButtonA || wr == WakeReason::Timer) {
      g_state.page_index = (uint8_t)((g_state.page_index + 1) % g_state.pages.size());
    } else if (wr == WakeReason::ButtonB) {
      g_state.page_index = (uint8_t)((g_state.page_index + g_state.pages.size() - 1)
                                      % g_state.pages.size());
    }
    nvs_save_progress(g_state.page_index, g_state.wakes_since_fetch);
  }

  render_current();
  if (WiFi.status() == WL_CONNECTED) {
    wifi_disconnect();
  }

#ifdef RCPAPER_DEV_CONSOLE
  dev::dev_buttons_init();
  Serial.println("[console] ready — type 'h' for commands. Deep sleep disabled.");
#else
  // Sleep for exactly one rotation interval — fetch cadence is decided by
  // the wakes_since_fetch counter at wake-time, not the sleep duration.
  enter_deep_sleep_until((uint64_t)time(nullptr) + ROTATE_INTERVAL_SEC);
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
