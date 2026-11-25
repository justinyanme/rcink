#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <time.h>

#include "config.h"
#include "dev/console_dump.h"
#include "display/st7305.h"
#include "display/pins_config.h"
#include "input/buttons.h"
#include "input/interaction_policy.h"
#include "input/navigation.h"
#include "net/fetch_diag.h"
#include "net/ntp.h"
#include "net/refresh_controller.h"
#include "net/refresh_flow.h"
#include "net/wifi.h"
#include "net/usage_config.h"
#include "power/network_policy.h"
#include "power/runtime_policy.h"
#include "power/sleep.h"
#include "power/wake_schedule.h"
#include "proto/parser.h"
#include "render/dashboard_schedule.h"
#include "render/deck_view.h"
#include "render/diagnostics.h"
#include "render/link_status.h"
#include "render/monument_persistence.h"
#include "render/monument_view.h"
#include "render/page.h"
#include "render/station_view.h"
#include "render/token_view.h"
#include "sense/battery.h"
#include "sense/clock.h"
#include "sense/rtc.h"
#include "sense/shtc3.h"
#include "store/nvs.h"

namespace rcpaper {

constexpr bool kShowClimateDashboard = (RCRLCD_SHOW_CLIMATE_DASHBOARD != 0);

// Desk climate Dashboard plus the Axiom Deck. RTC + SHTC3 keep Dashboard useful
// offline; network refreshes resync the RTC and update the secondary Deck.
DeviceState g_state;
UsageSnapshot g_usage;
TokenSnapshot g_token;
MonumentSnapshot g_monument;
BatteryReading g_battery;
Shtc3Reading g_env;
View g_view = View::Dashboard;
RuntimeState g_runtime = {RuntimeMode::Eco, 0, 0, false};
static bool g_carousel_pinned = false;
static HeatmapWindow g_heatmap_window = HeatmapWindow::Year;
static uint32_t g_carousel_last_switch_ms = 0;
static uint64_t g_last_env_minute = UINT64_MAX;

RTC_DATA_ATTR static uint32_t g_wake_count = 0;
RTC_DATA_ATTR static uint64_t g_last_dashboard_minute = UINT64_MAX;
// Runtime-badge redraws reuse this retained minute sample. Without it, a
// HOST/ACTIVE/ECO edge would violate the dashboard's minute cadence.
RTC_DATA_ATTR static Shtc3Reading g_dashboard_env;
RTC_DATA_ATTR static BatteryReading g_dashboard_battery;
RTC_DATA_ATTR static bool g_dashboard_snapshot_valid = false;
RTC_DATA_ATTR static uint64_t g_fetch_retry_not_before_s = 0;
RTC_DATA_ATTR static bool g_deck_frame_valid = false;
RTC_DATA_ATTR static bool g_last_deck_stale = false;
RTC_DATA_ATTR static bool g_usage_fetch_failed = false;
RTC_DATA_ATTR static bool g_token_fetch_failed = false;
RTC_DATA_ATTR static uint64_t g_token_last_ok_s = 0;
RTC_DATA_ATTR static uint64_t g_token_retry_at_s = 0;
RTC_DATA_ATTR static uint32_t g_usage_frame_fingerprint = 0;
RTC_DATA_ATTR static uint64_t g_usage_last_fetch_s = 0;
RTC_DATA_ATTR static uint32_t g_usage_fetch_fingerprint = 0;
RTC_DATA_ATTR static uint8_t g_usage_unchanged_fetches = 0;
RTC_DATA_ATTR static LinkFailure g_link_failure = LinkFailure::None;
RTC_DATA_ATTR static uint32_t g_retry_cooldown_s = 0;
RTC_DATA_ATTR static FetchDiag g_usage_last_diag;
RTC_DATA_ATTR static FetchDiag g_usage_last_err;
RTC_DATA_ATTR static FetchDiag g_token_last_diag;
RTC_DATA_ATTR static FetchDiag g_token_last_err;
static uint64_t g_usage_evaluated_at_s = UINT64_MAX;
static const char* g_last_result = "boot";
#if RCRLCD_USAGE_ENABLED
// HOST-only animation of the usage road view. ECO and ACTIVE always draw the
// static frame, which is also what deep sleep retains.
static constexpr uint32_t kUsageFrameMs = 125;
static constexpr uint32_t kUsageSweepMs = 900;
static bool g_usage_animation = true;
static bool g_usage_sweeping = false;
static uint32_t g_usage_sweep_start_ms = 0;
static uint32_t g_usage_last_frame_ms = 0;
// Advances once per quiet animation frame. The loop delivers frames every
// ~135 ms rather than exactly kUsageFrameMs, so stepped motion keyed to millis()
// would alias (the car would skip a bob every ~1.6 s); keyed to this, each frame
// moves exactly one step.
static uint32_t g_usage_tick = 0;
// BOOT asked for a sync that has not completed yet; the footer says SYNCING.
static bool g_usage_sync_requested = false;
static std::string g_usage_view_json;
static UsageView g_usage_view;
static bool g_token_sweeping = false;
static uint32_t g_token_sweep_start_ms = 0;
static uint32_t g_token_last_frame_ms = 0;
static std::string g_token_view_json;
static TokenView g_token_view;
static bool g_monument_sweeping = false;
static bool g_monument_last_colon = true;
static uint32_t g_monument_sweep_start_ms = 0;
static uint32_t g_monument_last_frame_ms = 0;
static std::string g_monument_view_json;
static MonumentView g_monument_view;
static bool g_station_sweeping = false;
static bool g_station_last_colon = true;
static uint32_t g_station_sweep_start_ms = 0;
static uint32_t g_station_last_frame_ms = 0;
static std::string g_station_view_json;
static StationView g_station_view;
#endif
static bool g_render_pending = false;

static_assert(FETCH_INTERVAL_SEC > 0, "FETCH_INTERVAL_SEC must be > 0");

static constexpr uint64_t kWakeButtonMask =
    1ULL << PIN_BUTTON_2;  // GPIO18 only; never GPIO0/BOOT.

#ifdef RCRLCD_DEV_CONSOLE
constexpr bool kAlwaysActive = true;
#else
constexpr bool kAlwaysActive = false;
#endif

static const char* view_name(View view) {
  return view == View::Dashboard ? "dashboard" : "deck";
}

static uint8_t view_to_nvs(View view) {
  return view == View::Dashboard ? 0 : 1;
}

static View view_from_nvs(uint8_t raw) {
  return raw == 1 ? View::Deck : View::Dashboard;
}

static bool usb_host_present() {
  // Arduino's HWCDC implementation derives this from 1 ms USB SOF packets.
  // It means a USB data host, not VBUS, a charger, or external power.
  return Serial.isPlugged();
}

static bool woke_from_key() {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 &&
         (esp_sleep_get_ext1_wakeup_status() & kWakeButtonMask) != 0;
}

static RefreshAppState refresh_app_state() {
  return {
      g_state,
      g_battery,
      g_runtime.mode,
      g_fetch_retry_not_before_s,
      g_last_result,
      g_usage,
      g_usage_fetch_failed,
      g_token,
      g_token_fetch_failed,
      g_token_last_ok_s,
      g_token_retry_at_s,
      g_monument,
      g_heatmap_window,
      g_link_failure,
      g_retry_cooldown_s,
      g_usage_last_diag,
      g_usage_last_err,
      g_token_last_diag,
      g_token_last_err,
  };
}

static void try_enter_deep_sleep() {
  if (buttons::any_down()) return;
  if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
  if (buttons::any_down()) return;
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);
  esp_sleep_enable_ext1_wakeup(kWakeButtonMask, ESP_EXT1_WAKEUP_ANY_LOW);
  // Final raw check protects both GPIO18 repeat wakes and GPIO0's boot strap.
  if (buttons::any_down()) return;
#if RCRLCD_USAGE_ENABLED
  g_usage_last_fetch_s = g_state.last_fetch_s;
  g_usage_fetch_fingerprint = g_state.last_fetch_fingerprint;
  g_usage_unchanged_fetches = g_state.unchanged_fetches;
#endif
  display_prepare_for_deep_sleep();
  deep_sleep_until(next_probe_wake_epoch((uint64_t)time(nullptr)));
}

#if RCRLCD_USAGE_ENABLED
static UsageSync usage_sync() {
  return {g_usage_fetch_failed, g_state.last_fetch_s, g_fetch_retry_not_before_s,
          g_usage_sync_requested, g_link_failure, g_retry_cooldown_s, WIFI_SSID};
}

static TokenSync token_sync() {
  const uint64_t retry = g_link_failure != LinkFailure::None
      ? g_fetch_retry_not_before_s
      : (g_token_retry_at_s > g_fetch_retry_not_before_s
          ? g_token_retry_at_s : g_fetch_retry_not_before_s);
  return {g_token_fetch_failed, g_token_last_ok_s, retry,
          g_usage_sync_requested, g_link_failure, g_retry_cooldown_s, WIFI_SSID};
}

static bool usage_animating() {
  return g_usage_animation && g_runtime.mode == RuntimeMode::Host;
}

static void start_usage_sweep() {
  if (!usage_animating()) return;
  g_usage_sweeping = true;
  g_usage_sweep_start_ms = millis();
}

static UsageFrame usage_frame() {
  UsageFrame frame;
  if (!usage_animating()) return frame;
  const uint32_t now = millis();
  frame.moving = true;
  frame.tick = g_usage_tick;
  frame.t_ms = now;
  if (g_usage_sweeping) {
    const uint32_t elapsed = now - g_usage_sweep_start_ms;
    if (elapsed >= kUsageSweepMs) {
      g_usage_sweeping = false;
    } else {
      const float rest = 1.0f - (float)elapsed / (float)kUsageSweepMs;
      frame.sweep = 1.0f - rest * rest * rest;  // ease-out cubic
    }
  }
  return frame;
}

static bool token_animating() {
  return g_usage_animation && g_runtime.mode == RuntimeMode::Host;
}

static void start_token_sweep() {
  if (!token_animating()) return;
  g_token_sweeping = true;
  g_token_sweep_start_ms = millis();
}

static TokenFrame token_frame() {
  TokenFrame frame;
  if (!token_animating() || !g_token_sweeping) return frame;
  const uint32_t elapsed = millis() - g_token_sweep_start_ms;
  if (elapsed >= kUsageSweepMs) {
    g_token_sweeping = false;
  } else {
    const float rest = 1.0f - (float)elapsed / (float)kUsageSweepMs;
    frame.sweep = 1.0f - rest * rest * rest;
  }
  return frame;
}

static bool monument_animating() {
  return g_usage_animation && g_runtime.mode == RuntimeMode::Host;
}

static void start_monument_sweep() {
  if (!monument_animating()) return;
  g_monument_sweeping = true;
  g_monument_last_colon = true;
  g_monument_sweep_start_ms = millis();
}

static MonumentFrame monument_frame() {
  MonumentFrame frame;
  if (!monument_animating()) return frame;
  if (g_monument_sweeping) {
    const uint32_t elapsed = millis() - g_monument_sweep_start_ms;
    if (elapsed >= kUsageSweepMs) {
      g_monument_sweeping = false;
    } else {
      const float rest = 1.0f - (float)elapsed / (float)kUsageSweepMs;
      frame.sweep = 1.0f - rest * rest * rest;
    }
    return frame;
  }
  frame.show_colon = ((millis() / 500) % 2) == 0;
  return frame;
}

// Draw exactly the model that was fingerprinted into the page record.
static bool draw_usage_page(const PageRecord& rec) {
  if (rec.layout_json != g_usage_view_json) {
    UsageView view;
    if (!decode_usage_view(rec.layout_json, view)) return false;
    g_usage_view = view;
    g_usage_view_json = rec.layout_json;
  }
  render_usage(g_usage_view, g_env, g_battery, g_runtime.mode, usage_frame(),
               g_carousel_pinned);
  return true;
}

static bool draw_token_page(const PageRecord& rec) {
  if (rec.layout_json != g_token_view_json) {
    TokenView view;
    if (!decode_token_view(rec.layout_json, view)) return false;
    g_token_view = view;
    g_token_view_json = rec.layout_json;
  }
  render_token(g_token_view, g_battery, g_runtime.mode, token_frame(),
               g_carousel_pinned);
  return true;
}

static bool draw_monument_page(const PageRecord& rec) {
  if (rec.layout_json != g_monument_view_json) {
    MonumentView view;
    if (!decode_monument_view(rec.layout_json, view)) return false;
    g_monument_view = view;
    g_monument_view_json = rec.layout_json;
  }
  const MonumentFrame frame = monument_frame();
  g_monument_last_colon = frame.show_colon;
  render_monument(g_monument_view, g_env, g_battery, g_runtime.mode, frame,
                  g_carousel_pinned);
  return true;
}

static void start_station_sweep() {
  if (!monument_animating()) return;
  g_station_sweeping = true;
  g_station_last_colon = true;
  g_station_sweep_start_ms = millis();
}

static StationFrame station_frame() {
  StationFrame frame;
  if (!monument_animating()) return frame;
  if (g_station_sweeping) {
    const uint32_t elapsed = millis() - g_station_sweep_start_ms;
    if (elapsed >= kUsageSweepMs) {
      g_station_sweeping = false;
    } else {
      const float rest = 1.0f - (float)elapsed / (float)kUsageSweepMs;
      frame.sweep = 1.0f - rest * rest * rest;
    }
    return frame;
  }
  frame.show_colon = ((millis() / 500) % 2) == 0;
  return frame;
}

static bool draw_station_page(const PageRecord& rec) {
  if (rec.layout_json != g_station_view_json) {
    StationView view;
    if (!decode_station_view(rec.layout_json, view)) return false;
    g_station_view = view;
    g_station_view_json = rec.layout_json;
  }
  const StationFrame frame = station_frame();
  g_station_last_colon = frame.show_colon;
  render_station(g_station_view, g_env, g_battery, g_runtime.mode, frame,
                 g_carousel_pinned);
  return true;
}
#endif

static bool maybe_sample_env(uint64_t now, bool force = false) {
  if (!force && !dashboard_sample_due(now, g_last_env_minute, g_env.ok)) {
    return false;
  }
  const Shtc3Reading fresh = shtc3_read();
  g_last_env_minute = now / 60;
  const bool changed = !g_env.ok || !fresh.ok ||
      lroundf(fresh.temp_c * 10.0f) != lroundf(g_env.temp_c * 10.0f) ||
      lroundf(fresh.rh) != lroundf(g_env.rh);
  g_env = fresh;
  return changed;
}

void render_dashboard_now(bool force_sample = false) {
  const uint64_t now = (uint64_t)time(nullptr);
  if (force_sample ||
      dashboard_sample_due(now, g_last_dashboard_minute,
                           g_dashboard_snapshot_valid)) {
    g_dashboard_env = shtc3_read();
    g_dashboard_battery = read_battery();
    g_dashboard_snapshot_valid = true;
    g_last_dashboard_minute = now / 60;
    Serial.printf("[dash] sampled T=%.1fC RH=%.0f%% env_ok=%d batt=%u%%/%umV\n",
                  g_dashboard_env.temp_c, g_dashboard_env.rh,
                  (int)g_dashboard_env.ok,
                  (unsigned)g_dashboard_battery.pct,
                  (unsigned)g_dashboard_battery.mv);
  }
  g_env = g_dashboard_env;
  g_battery = g_dashboard_battery;
  render_dashboard(g_dashboard_env, g_dashboard_battery, now, g_runtime.mode);
  g_deck_frame_valid = false;
}

static bool deck_is_stale(uint64_t now) {
#if RCRLCD_USAGE_ENABLED
  return false;  // Usage rows carry source freshness, not HTTP receipt freshness.
#else
  return g_state.last_fetch_s > 0 && now >= g_state.last_fetch_s &&
         now - g_state.last_fetch_s >= FETCH_INTERVAL_SEC;
#endif
}

void render_current() {
  g_battery = read_battery();
  if (g_state.pages.empty()) {
    g_last_result = "no-pages";
    render_error("no pages - waiting for refresh", g_runtime.mode);
    g_deck_frame_valid = false;
    return;
  }
  if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;
  const uint64_t now = (uint64_t)time(nullptr);
  const bool stale = deck_is_stale(now);

#if RCRLCD_USAGE_ENABLED
  maybe_sample_env(now);
  if (g_state.pages[g_state.page_index].id == kStationPageId) {
    update_station_page(g_state, g_usage, usage_sync(), g_token, g_monument,
                        token_sync(), now, g_runtime.mode, g_heatmap_window);
  } else if (g_state.pages[g_state.page_index].id == kMonumentPageId) {
    update_monument_page(g_state, g_monument, g_token, now, token_sync(),
                         g_runtime.mode, g_heatmap_window);
  }
  const PageRecord& rec = g_state.pages[g_state.page_index];
  if (rec.id == kStationPageId) {
    const uint32_t t0 = millis();
    if (!draw_station_page(rec)) {
      g_last_result = "station-view-invalid";
      render_error("station view decode failed", g_runtime.mode);
      g_deck_frame_valid = false;
      return;
    }
    Serial.printf("[render] station in %u ms\n", (unsigned)(millis() - t0));
  } else if (rec.id == kUsagePageId) {
    const uint32_t t0 = millis();
    if (!draw_usage_page(rec)) {
      g_last_result = "usage-view-invalid";
      render_error("usage view decode failed", g_runtime.mode);
      g_deck_frame_valid = false;
      return;
    }
    Serial.printf("[render] usage road in %u ms\n", (unsigned)(millis() - t0));
  } else if (rec.id == kTokenPageId) {
    const uint32_t t0 = millis();
    if (!draw_token_page(rec)) {
      g_last_result = "token-view-invalid";
      render_error("token view decode failed", g_runtime.mode);
      g_deck_frame_valid = false;
      return;
    }
    Serial.printf("[render] token bars in %u ms\n", (unsigned)(millis() - t0));
  } else if (rec.id == kMonumentPageId) {
    const uint32_t t0 = millis();
    if (!draw_monument_page(rec)) {
      g_last_result = "monument-view-invalid";
      render_error("monument view decode failed", g_runtime.mode);
      g_deck_frame_valid = false;
      return;
    }
    Serial.printf("[render] monument in %u ms\n", (unsigned)(millis() - t0));
  } else
#else
  const PageRecord& rec = g_state.pages[g_state.page_index];
#endif
  {
    LayoutNode root;
    if (!parse_page_layout(rec, root)) {
      g_last_result = "parse-failed";
      render_error("layout_json parse failed", g_runtime.mode);
      g_deck_frame_valid = false;
      return;
    }
    if (!render_page(rec, root, stale, g_state, g_battery, g_runtime.mode)) {
      g_deck_frame_valid = false;
      return;
    }
  }
  g_deck_frame_valid = true;
  g_last_deck_stale = stale;
#if RCRLCD_USAGE_ENABLED
  g_usage_frame_fingerprint = usage_page_fingerprint(rec);
#endif

  if (g_state.last_rendered_id != rec.id ||
      g_state.last_rendered_data_ts != rec.data_ts) {
    nvs_record_rendered_page(g_state, rec);
  }
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
#if RCRLCD_USAGE_ENABLED
  char allow_buf[128] = {};
  char token_buf[128] = {};
  {
    char last_buf[64] = {}, err_buf[64] = {};
    format_fetch_diag(g_usage_last_diag, last_buf, sizeof(last_buf), false);
    if (g_usage_last_err.attempted && g_usage_last_diag.ok) {
      format_fetch_diag(g_usage_last_err, err_buf, sizeof(err_buf));
      snprintf(allow_buf, sizeof(allow_buf), "%s | err:%s", last_buf, err_buf);
    } else {
      snprintf(allow_buf, sizeof(allow_buf), "%s", last_buf);
    }
    snap.allowance_diag = allow_buf;
  }
  {
    char last_buf[64] = {}, err_buf[64] = {};
    format_fetch_diag(g_token_last_diag, last_buf, sizeof(last_buf), false);
    if (g_token_last_err.attempted && g_token_last_diag.ok) {
      format_fetch_diag(g_token_last_err, err_buf, sizeof(err_buf));
      snprintf(token_buf, sizeof(token_buf), "%s | err:%s", last_buf, err_buf);
    } else {
      snprintf(token_buf, sizeof(token_buf), "%s", last_buf);
    }
    snap.token_diag = token_buf;
  }
#endif
  PageRecord rec;
  rec.id = "diagnostics";
  rec.order = -1;
  rec.data_ts = (uint64_t)time(nullptr);
  LayoutNode root = build_diagnostics_page(snap);
  render_page(rec, root, false, g_state, g_battery, g_runtime.mode);
  g_deck_frame_valid = false;
}

void render_current_view() {
  g_render_pending = false;
  if (g_view == View::Dashboard) render_dashboard_now();
  else render_current();
}

void set_view(View view) {
  if (g_view == view) return;
  g_view = view;
  g_state.view_id = view_to_nvs(view);
  nvs_save_view(g_state.view_id);
  Serial.printf("[view] -> %s\n", view_name(view));
}

void enter_view(View view) {
  set_view(view);
  render_current_view();
}

void toggle_view() {
  if (!kShowClimateDashboard) {
    Serial.println("[view] climate dashboard hidden");
    if (g_view != View::Deck) enter_view(View::Deck);
    return;
  }
  enter_view(g_view == View::Dashboard ? View::Deck : View::Dashboard);
}

void apply_navigation(const NavigationState& next) {
  g_carousel_last_switch_ms = millis();
#if RCRLCD_USAGE_ENABLED
  if (next.view == View::Deck &&
      (g_view != View::Deck || next.page_index != g_state.page_index) &&
      next.page_index < g_state.pages.size()) {
    const std::string& id = g_state.pages[next.page_index].id;
    if (id == kStationPageId) start_station_sweep();
    else if (id == kTokenPageId) start_token_sweep();
    else if (id == kMonumentPageId) start_monument_sweep();
    else start_usage_sweep();
  }
#endif
  if (next.page_index != g_state.page_index) {
    g_state.page_index = next.page_index;
    nvs_save_progress(g_state.page_index);
  }
  set_view(next.view);
}

void navigate_next() {
  apply_navigation(navigation_after_next_press(
      g_view, g_state.page_index, g_state.pages.size(), kShowClimateDashboard));
}

void navigate_previous() {
  apply_navigation(navigation_after_previous_press(
      g_view, g_state.page_index, g_state.pages.size(), kShowClimateDashboard));
}

#if RCRLCD_USAGE_ENABLED
// BOOT syncs the visible Deck page in place; from climate it opens Allowance.
// Both GETs are forced in the same worker/Wi-Fi session. An in-flight refresh is
// joined where possible rather than doubled. The caller redraws.
static void request_usage_sync() {
  apply_navigation(navigation_after_sync_press(
      g_view, g_state.page_index, g_state.pages.size()));
  g_usage_sync_requested = true;
  start_network_refresh(refresh_app_state(), true);
  if (!refresh_busy() && !refresh_token_pending()) {
    g_usage_sync_requested = false;  // the task could not start
  }
  const uint64_t now = (uint64_t)time(nullptr);
  update_station_page(g_state, g_usage, usage_sync(), g_token, g_monument,
                      token_sync(), now, g_runtime.mode, g_heatmap_window);
  update_usage_page(g_state, g_usage, now, usage_sync());
  update_token_page(g_state, g_token, now, token_sync());
  update_monument_page(g_state, g_monument, g_token, now, token_sync(),
                       g_runtime.mode, g_heatmap_window);
}
#endif

static DeckViewKey deck_visible_state() {
  DeckViewKey visible = {
      "", 0, g_state.page_index, g_state.pages.size(),
      g_battery.pct, g_battery.ok,
      deck_is_stale((uint64_t)time(nullptr)),
      g_runtime.mode, st7305_dark_mode(), g_carousel_pinned,
      (int16_t)lroundf(g_env.temp_c * 10.0f),
      (uint8_t)lroundf(std::max(0.0f, std::min(100.0f, g_env.rh))),
      g_env.ok,
  };
  if (g_state.page_index < g_state.pages.size()) {
    const PageRecord& page = g_state.pages[g_state.page_index];
    visible.page_id = page.id;
    visible.data_ts = page.data_ts;
  }
  return visible;
}

static bool dashboard_minute_due() {
  return g_view == View::Dashboard &&
         dashboard_sample_due((uint64_t)time(nullptr),
                              g_last_dashboard_minute,
                              g_dashboard_snapshot_valid);
}

static bool deck_stale_edge_due() {
  return g_view == View::Deck && g_deck_frame_valid &&
         deck_is_stale((uint64_t)time(nullptr)) != g_last_deck_stale;
}

static bool poll_refresh_completion() {
  const DeckViewKey before = deck_visible_state();
  bool usage_page_changed = false;
  bool token_succeeded = false;
  bool allowance_page_changed = false;
  bool monument_succeeded = false;
  if (!finish_network_refresh(refresh_app_state(), usage_page_changed,
                              token_succeeded, allowance_page_changed,
                              monument_succeeded)) {
    return false;
  }
#if RCRLCD_USAGE_ENABLED
  // A very late BOOT may have missed the active worker's Token GET. Keep the
  // requested-sync footer up while the queued follow-up run starts.
  g_usage_sync_requested = refresh_token_pending();
  if (g_usage_sync_requested) {
    const uint64_t now = (uint64_t)time(nullptr);
    update_station_page(g_state, g_usage, usage_sync(), g_token, g_monument,
                        token_sync(), now, g_runtime.mode, g_heatmap_window);
    update_usage_page(g_state, g_usage, now, usage_sync());
    update_token_page(g_state, g_token, now, token_sync());
    update_monument_page(g_state, g_monument, g_token, now, token_sync(),
                         g_runtime.mode, g_heatmap_window);
    usage_page_changed = true;
  }
  if (g_view == View::Deck && g_state.page_index < g_state.pages.size()) {
    const std::string& id = g_state.pages[g_state.page_index].id;
    if (id == kStationPageId &&
        (monument_succeeded || token_succeeded ||
         (allowance_page_changed && !g_usage_fetch_failed))) {
      start_station_sweep();
    }
    if (id == kTokenPageId && token_succeeded) start_token_sweep();
    if (id == kMonumentPageId && (monument_succeeded || token_succeeded)) {
      start_monument_sweep();
    }
    if (id == kUsagePageId && allowance_page_changed && !g_usage_fetch_failed) {
      start_usage_sweep();
    }
  }
#endif
  return g_view == View::Dashboard
             ? dashboard_minute_due()
             : usage_page_changed || before != deck_visible_state();
}

static bool poll_usage_freshness() {
#if RCRLCD_USAGE_ENABLED
  const uint64_t now = (uint64_t)time(nullptr);
  if (now == g_usage_evaluated_at_s) return false;
  g_usage_evaluated_at_s = now;
  const bool station_changed =
      update_station_page(g_state, g_usage, usage_sync(), g_token, g_monument,
                          token_sync(), now, g_runtime.mode, g_heatmap_window);
  const bool allowance_changed =
      update_usage_page(g_state, g_usage, now, usage_sync());
  const bool token_changed =
      update_token_page(g_state, g_token, now, token_sync());
  const bool monument_changed =
      update_monument_page(g_state, g_monument, g_token, now, token_sync(),
                           g_runtime.mode, g_heatmap_window);
  const bool env_changed = g_view == View::Deck && maybe_sample_env(now);
  if (g_view != View::Deck || g_state.page_index >= g_state.pages.size()) {
    return false;
  }
  const std::string& id = g_state.pages[g_state.page_index].id;
  if (id == kStationPageId) return station_changed || env_changed;
  if (id == kUsagePageId) return allowance_changed || env_changed;
  if (id == kTokenPageId) return token_changed;
  if (id == kMonumentPageId) return monument_changed || env_changed;
  return station_changed || allowance_changed || token_changed || monument_changed;
#else
  return false;
#endif
}

// Quiet in-place frames; content changes still go through render_current().
static void poll_usage_animation() {
#if RCRLCD_USAGE_ENABLED
  if (g_view != View::Deck || !g_deck_frame_valid || !usage_animating() ||
      g_state.page_index >= g_state.pages.size() ||
      g_state.pages[g_state.page_index].id != kUsagePageId) {
    return;
  }
  const uint32_t now = millis();
  if (now - g_usage_last_frame_ms < kUsageFrameMs) return;
  g_usage_last_frame_ms = now;
  g_usage_tick++;
  draw_usage_page(g_state.pages[g_state.page_index]);
#endif
}

// Token has no idle motion: HOST draws only the 0.9 s entry/sync sweep, then
// leaves the final static frame untouched. ECO/ACTIVE always take sweep == 1.
static void poll_token_animation() {
#if RCRLCD_USAGE_ENABLED
  if (g_view != View::Deck || !g_deck_frame_valid || !g_token_sweeping ||
      !token_animating() || g_state.page_index >= g_state.pages.size() ||
      g_state.pages[g_state.page_index].id != kTokenPageId) return;
  const uint32_t now = millis();
  if (now - g_token_last_frame_ms < kUsageFrameMs) return;
  g_token_last_frame_ms = now;
  draw_token_page(g_state.pages[g_state.page_index]);
#endif
}

static void poll_monument_animation() {
#if RCRLCD_USAGE_ENABLED
  if (g_view != View::Deck || !g_deck_frame_valid || !monument_animating() ||
      g_state.page_index >= g_state.pages.size()) {
    return;
  }
  const std::string& id = g_state.pages[g_state.page_index].id;
  const uint32_t now = millis();
  if (id == kStationPageId) {
    if (g_station_sweeping) {
      if (now - g_station_last_frame_ms < kUsageFrameMs) return;
      g_station_last_frame_ms = now;
      draw_station_page(g_state.pages[g_state.page_index]);
      return;
    }
    if (station_frame().show_colon != g_station_last_colon) {
      g_station_last_frame_ms = now;
      draw_station_page(g_state.pages[g_state.page_index]);
    }
    return;
  }
  if (id != kMonumentPageId) return;
  if (g_monument_sweeping) {
    if (now - g_monument_last_frame_ms < kUsageFrameMs) return;
    g_monument_last_frame_ms = now;
    draw_monument_page(g_state.pages[g_state.page_index]);
    return;
  }
  if (monument_frame().show_colon != g_monument_last_colon) {
    g_monument_last_frame_ms = now;
    draw_monument_page(g_state.pages[g_state.page_index]);
  }
#endif
}

static void toggle_carousel_pin(const char* source) {
  g_carousel_pinned = !g_carousel_pinned;
  nvs_save_carousel_pinned(g_carousel_pinned);
  g_carousel_last_switch_ms = millis();
  Serial.printf("%s -> carousel pinned=%d\n", source, (int)g_carousel_pinned);
}

#if RCRLCD_USAGE_ENABLED
static void cycle_heatmap_window(const char* source) {
  g_heatmap_window = next_heatmap_window(g_heatmap_window);
  save_heatmap_window(g_heatmap_window);
  g_station_sweeping = false;
  g_monument_sweeping = false;
  g_carousel_last_switch_ms = millis();
  const uint64_t now = (uint64_t)time(nullptr);
  update_station_page(g_state, g_usage, usage_sync(), g_token, g_monument,
                      token_sync(), now, g_runtime.mode, g_heatmap_window);
  update_monument_page(g_state, g_monument, g_token, now, token_sync(),
                       g_runtime.mode, g_heatmap_window);
  Serial.printf("%s -> heatmap window=%s\n", source,
                heatmap_window_label(g_heatmap_window));
}
#endif

struct InteractionDecision {
  bool screen_changed;
  bool sleep_requested;
};

static InteractionDecision poll_interaction() {
  const buttons::Events events = buttons::poll();
  bool activity = events.activity;
  if (events.key_tap) {
    Serial.println("[btn] KEY -> next");
    navigate_next();
    activity = true;
  }
  if (events.key_long) {
    const std::string active_id =
        g_view == View::Deck && g_state.page_index < g_state.pages.size()
            ? g_state.pages[g_state.page_index].id
            : std::string();
    if (key_long_action_for_page(g_view, active_id, RCRLCD_USAGE_ENABLED) ==
        KeyLongAction::CycleHeatmapWindow) {
#if RCRLCD_USAGE_ENABLED
      cycle_heatmap_window("[btn] KEY long");
#endif
    } else {
      toggle_carousel_pin("[btn] KEY long");
    }
    activity = true;
  }
  if (events.chord) {
    toggle_carousel_pin("[btn] KEY+BOOT chord");
    activity = true;
  }
  if (events.boot_tap) {
#if RCRLCD_USAGE_ENABLED
    Serial.println("[btn] BOOT -> sync now");
    request_usage_sync();
#else
    Serial.println("[btn] BOOT -> previous");
    navigate_previous();
#endif
    activity = true;
  }
  if (events.boot_long) {
    const bool dark = !st7305_dark_mode();
    st7305_set_dark_mode(dark);
    nvs_save_dark_mode(dark);
    Serial.printf("[btn] BOOT long -> dark=%d\n", (int)dark);
    activity = true;
  }

  const RuntimeMode previous = g_runtime.mode;
  const RuntimeInput input = {
      millis(), kAlwaysActive, usb_host_present(), activity,
      buttons::any_down(),
  };
  const RuntimeDecision decision = advance_runtime_state(g_runtime, input);
  g_runtime = decision.state;
  if (decision.mode_changed) {
    adjust_refresh_gate_for_runtime_change(refresh_app_state(), previous);
    if (g_runtime.mode == RuntimeMode::Host) {
      g_carousel_last_switch_ms = millis();
    }
    Serial.printf("[runtime] %s -> %s\n", runtime_mode_name(previous),
                  runtime_mode_name(g_runtime.mode));
  }
  return {
      interaction_changes_visible_state(events, decision.mode_changed),
      decision.sleep_requested,
  };
}

void console_help() {
  Serial.println(
      "[console] f fetch, r render, d diagnostics, m toggle-view, n next, "
      "v previous, l pin-carousel, w heatmap-window, t read-sensors, i invert, "
      "a usage-animation, s state, q layout, p frame, c clear, R reboot, h help");
}

void console_dispatch(char c) {
  switch (c) {
    case 'h':
    case '?':
      console_help();
      break;
    case 'f':
      Serial.println("[console] network refresh");
      g_carousel_last_switch_ms = millis();
#if RCRLCD_USAGE_ENABLED
      request_usage_sync();  // same path as BOOT
      render_current_view();
#else
      start_network_refresh(refresh_app_state());
#endif
      break;
    case 'r':
      Serial.println("[console] render");
      render_current_view();
      break;
    case 'd':
      Serial.println("[console] diagnostics");
      render_diagnostics();
      break;
    case 'm':
      toggle_view();
      break;
    case 'n':
      navigate_next();
      render_current_view();
      break;
    case 'v':
      navigate_previous();
      render_current_view();
      break;
    case 'l':
      toggle_carousel_pin("[console]");
      render_current_view();
      break;
    case 'w':
#if RCRLCD_USAGE_ENABLED
      cycle_heatmap_window("[console]");
      render_current_view();
#else
      Serial.println("[console] usage disabled");
#endif
      break;
    case 't':
      if (g_view == View::Dashboard) {
        render_dashboard_now(true);
      } else {
        g_env = shtc3_read();
        g_last_env_minute = (uint64_t)time(nullptr) / 60;
        g_battery = read_battery();
        render_current_view();
      }
      break;
    case 'i': {
      const bool dark = !st7305_dark_mode();
      st7305_set_dark_mode(dark);
      nvs_save_dark_mode(dark);
      render_current_view();
      break;
    }
    case 'a':
#if RCRLCD_USAGE_ENABLED
      g_usage_animation = !g_usage_animation;
      g_usage_sweeping = false;
      g_token_sweeping = false;
      g_monument_sweeping = false;
      g_station_sweeping = false;
      Serial.printf("[console] usage animation %s\n", g_usage_animation ? "on" : "off");
      render_current_view();
#else
      Serial.println("[console] usage disabled");
#endif
      break;
    case 's':
      console_dump_state(g_view, g_runtime.mode, usb_host_present(), g_state,
                         g_env, g_battery
#if RCRLCD_USAGE_ENABLED
                         , &g_usage_last_diag, &g_usage_last_err,
                         &g_token_last_diag, &g_token_last_err
#endif
                         );
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
      if (refresh_busy()) {
        Serial.println("[console] frame unavailable: refresh running");
      } else {
        console_dump_frame(g_view, g_state);
      }
      break;
    case 'c':
      Serial.println("[console] clear");
      clear_screen();
      g_deck_frame_valid = false;
      break;
    case 'R':
      if (buttons::any_down()) {
        Serial.println("[console] reboot rejected: button held");
        break;
      }
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

}  // namespace rcpaper

using namespace rcpaper;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] rcink rlcd (Waveshare ESP32-S3-RLCD-4.2)");
  refresh_flow_init();
  g_wake_count++;

  apply_local_tz(LOCAL_TZ);
  Wire.begin(I2C_SDA, I2C_SCL, 400000);  // shared SHTC3 + PCF85063 bus
  const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
  const bool key_wake = woke_from_key();
  const bool resumed_from_deep_sleep =
      wake == ESP_SLEEP_WAKEUP_TIMER || wake == ESP_SLEEP_WAKEUP_EXT1;
  if (!resumed_from_deep_sleep) {
    g_last_dashboard_minute = UINT64_MAX;
    g_last_env_minute = UINT64_MAX;
    g_dashboard_snapshot_valid = false;
    g_fetch_retry_not_before_s = 0;
    g_deck_frame_valid = false;
    g_link_failure = LinkFailure::None;
    g_retry_cooldown_s = 0;
    g_usage_last_diag = FetchDiag{};
    g_usage_last_err = FetchDiag{};
    g_token_last_diag = FetchDiag{};
    g_token_last_err = FetchDiag{};
  }
  display_init(panel_startup_for_deep_sleep_wake(resumed_from_deep_sleep));
  st7305_set_dark_mode(nvs_load_dark_mode(RCRLCD_DEFAULT_DARK_MODE != 0));
  g_carousel_pinned = nvs_load_carousel_pinned(false);
  g_heatmap_window = load_heatmap_window();
  const buttons::WakeKeyDisposition wake_key = buttons::init(key_wake);

  rtc_begin();
  shtc3_begin();
  seed_clock_from_rtc();
  g_battery = read_battery();
  if (g_dashboard_snapshot_valid) g_env = g_dashboard_env;
  maybe_sample_env((uint64_t)time(nullptr), !g_env.ok);
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d env_ok=%d T=%.1f RH=%.0f\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok,
                (int)g_env.ok, g_env.temp_c, g_env.rh);

  g_state = nvs_load();
  const bool host = usb_host_present();
  const RuntimeBootInput runtime_boot = {
      millis(),
      kAlwaysActive,
      host,
      key_wake,
  };
  g_runtime = initial_runtime_state(runtime_boot);
  g_carousel_last_switch_ms = millis();
  if (g_runtime.mode == RuntimeMode::Host) {
    g_fetch_retry_not_before_s = 0;
    g_retry_cooldown_s = 0;
  }
#if RCRLCD_USAGE_ENABLED
  // Do not interpret the legacy Axiom pages cache as an allowance snapshot.
  decode_usage(nvs_load_usage(), g_usage);
  decode_token(nvs_load_token(), g_token);
  decode_monument(nvs_load_monument(), g_monument);
  if (!resumed_from_deep_sleep) {
    g_usage_fetch_failed = false;
    g_token_fetch_failed = false;
    g_token_last_ok_s = 0;
    g_token_retry_at_s = 0;
    g_usage_last_fetch_s = 0;
    g_usage_fetch_fingerprint = 0;
    g_usage_unchanged_fetches = 0;
  }
  if (g_runtime.mode == RuntimeMode::Host) {
    g_token_retry_at_s = token_retry_on_host_entry(
        g_token_retry_at_s, g_token_fetch_failed);
  }
  g_state.last_fetch_s = g_usage_last_fetch_s;
  g_state.last_fetch_fingerprint = g_usage_fetch_fingerprint;
  g_state.unchanged_fetches = g_usage_unchanged_fetches;
  const uint64_t now = (uint64_t)time(nullptr);
  g_state.pages = {
      station_page(g_usage, usage_sync(), g_token, g_monument, token_sync(),
                   now, g_runtime.mode, g_heatmap_window),
      usage_page(g_usage, now, usage_sync()),
      token_page(g_token, now, token_sync()),
      monument_page(g_monument, g_token, now, token_sync(), g_runtime.mode,
                    g_heatmap_window),
  };
  if (g_state.page_index >= g_state.pages.size()) g_state.page_index = 0;
#endif
  const uint8_t startup_view = startup_view_id(
      g_state.view_id, RCRLCD_USAGE_ENABLED, g_state.view_saved,
      kShowClimateDashboard);
  if (g_state.view_id != startup_view || !g_state.view_saved) {
    g_state.view_id = startup_view;
    nvs_save_view(g_state.view_id);
  }
  g_view = view_from_nvs(g_state.view_id);
  Serial.printf("[view] loaded %s\n", view_name(g_view));

  if (wake_key == buttons::WakeKeyDisposition::ImmediateTap) navigate_next();
#if RCRLCD_USAGE_ENABLED
  if (!resumed_from_deep_sleep && g_view == View::Deck) {
    const std::string& pid = g_state.pages[g_state.page_index].id;
    if (pid == kStationPageId) start_station_sweep();
    else if (pid == kTokenPageId) start_token_sweep();
    else if (pid == kMonumentPageId) start_monument_sweep();
  }
#endif

  g_render_pending = !resumed_from_deep_sleep || key_wake ||
                     g_runtime.mode != RuntimeMode::Eco ||
                     dashboard_minute_due();
#if RCRLCD_USAGE_ENABLED
  if (g_view == View::Deck && (!g_deck_frame_valid ||
      usage_page_fingerprint(g_state.pages[g_state.page_index]) != g_usage_frame_fingerprint)) {
    g_render_pending = true;
  }
#endif
  Serial.printf("[wake] cause=%d key=%d host=%d runtime=%s pinned=%d window=%s render=%d\n",
                (int)wake, (int)key_wake, (int)host,
                runtime_mode_name(g_runtime.mode), (int)g_carousel_pinned,
                heatmap_window_label(g_heatmap_window),
                (int)g_render_pending);
  if (host || kAlwaysActive) {
    console_help();
    Serial.println("[console] ready");
  }
}

void loop() {
  const bool refresh_changed_screen = poll_refresh_completion();
  const bool freshness_changed_screen = poll_usage_freshness();
  const InteractionDecision interaction = poll_interaction();
  if (refresh_changed_screen || freshness_changed_screen || interaction.screen_changed) {
    render_current_view();
    delay(20);
    return;
  }

  bool used_console = false;
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    console_dispatch((char)c);
    used_console = true;
  }
  if (used_console) {
    delay(20);
    return;
  }

  if (g_render_pending || dashboard_minute_due() || deck_stale_edge_due()) {
    render_current_view();
    delay(20);
    return;
  }

  if (g_view == View::Deck &&
      host_carousel_due(g_runtime.mode == RuntimeMode::Host, g_carousel_pinned,
                        refresh_busy(), buttons::any_down(),
                        g_state.pages.size(), millis(),
                        g_carousel_last_switch_ms)) {
    Serial.println("[auto] host carousel -> next");
    navigate_next();
    render_current_view();
    delay(20);
    return;
  }

  poll_usage_animation();
  poll_token_animation();
  poll_monument_animation();

  if (!refresh_busy() && !buttons::any_down() && !g_runtime.host_missing &&
      refresh_due_now(refresh_app_state())) {
    Serial.println("[auto] network refresh due");
    start_network_refresh(refresh_app_state());
    delay(20);
    return;
  }

  if (interaction.sleep_requested && !refresh_busy()) try_enter_deep_sleep();
  delay(20);
}
