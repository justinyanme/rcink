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
#include "net/ntp.h"
#include "net/refresh_controller.h"
#include "net/refresh_flow.h"
#include "net/wifi.h"
#include "net/usage_config.h"
#include "power/runtime_policy.h"
#include "power/sleep.h"
#include "power/wake_schedule.h"
#include "proto/parser.h"
#include "render/dashboard_schedule.h"
#include "render/deck_view.h"
#include "render/diagnostics.h"
#include "render/page.h"
#include "sense/battery.h"
#include "sense/clock.h"
#include "sense/rtc.h"
#include "sense/shtc3.h"
#include "store/nvs.h"

namespace rcpaper {

// Desk climate Dashboard plus the Axiom Deck. RTC + SHTC3 keep Dashboard useful
// offline; network refreshes resync the RTC and update the secondary Deck.
DeviceState g_state;
UsageSnapshot g_usage;
BatteryReading g_battery;
Shtc3Reading g_env;
View g_view = View::Dashboard;
RuntimeState g_runtime = {RuntimeMode::Eco, 0, 0, false};

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
RTC_DATA_ATTR static uint32_t g_usage_frame_fingerprint = 0;
RTC_DATA_ATTR static uint64_t g_usage_last_fetch_s = 0;
RTC_DATA_ATTR static uint32_t g_usage_fetch_fingerprint = 0;
RTC_DATA_ATTR static uint8_t g_usage_unchanged_fetches = 0;
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
          g_usage_sync_requested};
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

// Draw exactly the model that was fingerprinted into the page record.
static bool draw_usage_page(const PageRecord& rec) {
  if (rec.layout_json != g_usage_view_json) {
    UsageView view;
    if (!decode_usage_view(rec.layout_json, view)) return false;
    g_usage_view = view;
    g_usage_view_json = rec.layout_json;
  }
  render_usage(g_usage_view, g_battery, g_runtime.mode, usage_frame());
  return true;
}
#endif

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
  const PageRecord& rec = g_state.pages[g_state.page_index];
  const uint64_t now = (uint64_t)time(nullptr);
  const bool stale = deck_is_stale(now);

#if RCRLCD_USAGE_ENABLED
  if (rec.id == kUsagePageId) {
    const uint32_t t0 = millis();
    if (!draw_usage_page(rec)) {
      g_last_result = "usage-view-invalid";
      render_error("usage view decode failed", g_runtime.mode);
      g_deck_frame_valid = false;
      return;
    }
    Serial.printf("[render] usage road in %u ms\n", (unsigned)(millis() - t0));
  } else
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
  enter_view(g_view == View::Dashboard ? View::Deck : View::Dashboard);
}

void apply_navigation(const NavigationState& next) {
#if RCRLCD_USAGE_ENABLED
  if (next.view == View::Deck && g_view != View::Deck) start_usage_sweep();
#endif
  if (next.page_index != g_state.page_index) {
    g_state.page_index = next.page_index;
    nvs_save_progress(g_state.page_index);
  }
  set_view(next.view);
}

void navigate_next() {
  apply_navigation(navigation_after_next_press(
      g_view, g_state.page_index, g_state.pages.size()));
}

void navigate_previous() {
  apply_navigation(navigation_after_previous_press(
      g_view, g_state.page_index, g_state.pages.size()));
}

#if RCRLCD_USAGE_ENABLED
// BOOT with usage: Usage <-> climate needs only KEY, so BOOT means "show usage
// and sync now". It ignores the poll/retry gate; a sync already in flight is
// awaited rather than doubled. The caller redraws.
static void request_usage_sync() {
  apply_navigation({View::Deck, 0});
  g_usage_sync_requested = true;
  start_network_refresh(refresh_app_state());
  if (!refresh_busy()) g_usage_sync_requested = false;  // the task could not start
  update_usage_page(g_state, g_usage, (uint64_t)time(nullptr), usage_sync());
}
#endif

static DeckViewKey deck_visible_state() {
  DeckViewKey visible = {
      "", 0, g_state.page_index, g_state.pages.size(),
      g_battery.pct, g_battery.ok,
      deck_is_stale((uint64_t)time(nullptr)),
      g_runtime.mode, st7305_dark_mode(),
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
  if (!finish_network_refresh(refresh_app_state(), usage_page_changed)) return false;
#if RCRLCD_USAGE_ENABLED
  g_usage_sync_requested = false;  // the completion already redrew without SYNCING
  if (usage_page_changed && !g_usage_fetch_failed && g_view == View::Deck) start_usage_sweep();
#endif
  return g_view == View::Dashboard ? dashboard_minute_due()
                                   : usage_page_changed || before != deck_visible_state();
}

static bool poll_usage_freshness() {
#if RCRLCD_USAGE_ENABLED
  const uint64_t now = (uint64_t)time(nullptr);
  if (now == g_usage_evaluated_at_s) return false;
  g_usage_evaluated_at_s = now;
  return update_usage_page(g_state, g_usage, now, usage_sync()) &&
         g_view == View::Deck;
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

  const RuntimeMode previous = g_runtime.mode;
  const RuntimeInput input = {
      millis(), kAlwaysActive, usb_host_present(), activity,
      buttons::any_down(),
  };
  const RuntimeDecision decision = advance_runtime_state(g_runtime, input);
  g_runtime = decision.state;
  if (decision.mode_changed) {
    adjust_refresh_gate_for_runtime_change(refresh_app_state(), previous);
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
      "v previous, t read-sensors, i invert, a usage-animation, s state, "
      "q layout, p frame, c clear, R reboot, h help");
}

void console_dispatch(char c) {
  switch (c) {
    case 'h':
    case '?':
      console_help();
      break;
    case 'f':
      Serial.println("[console] network refresh");
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
    case 't':
      if (g_view == View::Dashboard) {
        render_dashboard_now(true);
      } else {
        g_env = shtc3_read();
        g_battery = read_battery();
      }
      break;
    case 'i':
      st7305_set_dark_mode(!st7305_dark_mode());
      render_current_view();
      break;
    case 'a':
#if RCRLCD_USAGE_ENABLED
      g_usage_animation = !g_usage_animation;
      g_usage_sweeping = false;
      Serial.printf("[console] usage animation %s\n", g_usage_animation ? "on" : "off");
      render_current_view();
#else
      Serial.println("[console] usage disabled");
#endif
      break;
    case 's':
      console_dump_state(g_view, g_runtime.mode, usb_host_present(), g_state,
                         g_env, g_battery);
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
    g_dashboard_snapshot_valid = false;
    g_fetch_retry_not_before_s = 0;
    g_deck_frame_valid = false;
  }
  display_init(panel_startup_for_deep_sleep_wake(resumed_from_deep_sleep));
  buttons::init(key_wake);

  rtc_begin();
  shtc3_begin();
  seed_clock_from_rtc();
  g_battery = read_battery();
  if (g_dashboard_snapshot_valid) g_env = g_dashboard_env;
  Serial.printf("[sense] batt=%u%% (%u mV) ok=%d\n",
                (unsigned)g_battery.pct, (unsigned)g_battery.mv, g_battery.ok);

  g_state = nvs_load();
#if RCRLCD_USAGE_ENABLED
  // Do not interpret the legacy Axiom pages cache as an allowance snapshot.
  decode_usage(nvs_load_usage(), g_usage);
  if (!resumed_from_deep_sleep) {
    g_usage_fetch_failed = false;
    g_usage_last_fetch_s = 0;
    g_usage_fetch_fingerprint = 0;
    g_usage_unchanged_fetches = 0;
  }
  g_state.last_fetch_s = g_usage_last_fetch_s;
  g_state.last_fetch_fingerprint = g_usage_fetch_fingerprint;
  g_state.unchanged_fetches = g_usage_unchanged_fetches;
  g_state.pages = {usage_page(g_usage, (uint64_t)time(nullptr), usage_sync())};
  g_state.page_index = 0;
#endif
  const uint8_t startup_view = startup_view_id(
      g_state.view_id, RCRLCD_USAGE_ENABLED, g_state.view_saved);
  if (g_state.view_id != startup_view || !g_state.view_saved) {
    g_state.view_id = startup_view;
    nvs_save_view(g_state.view_id);
  }
  g_view = view_from_nvs(g_state.view_id);
  Serial.printf("[view] loaded %s\n", view_name(g_view));

  const bool host = usb_host_present();
  const RuntimeBootInput runtime_boot = {
      millis(),
      kAlwaysActive,
      host,
      key_wake,
  };
  g_runtime = initial_runtime_state(runtime_boot);
  if (g_runtime.mode == RuntimeMode::Host) {
    g_fetch_retry_not_before_s = 0;
  }
  if (key_wake) navigate_next();

  g_render_pending = !resumed_from_deep_sleep || key_wake ||
                     g_runtime.mode != RuntimeMode::Eco ||
                     dashboard_minute_due();
#if RCRLCD_USAGE_ENABLED
  if (g_view == View::Deck && (!g_deck_frame_valid ||
      usage_page_fingerprint(g_state.pages[0]) != g_usage_frame_fingerprint)) {
    g_render_pending = true;
  }
#endif
  Serial.printf("[wake] cause=%d key=%d host=%d runtime=%s render=%d\n",
                (int)wake, (int)key_wake, (int)host,
                runtime_mode_name(g_runtime.mode), (int)g_render_pending);
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

  poll_usage_animation();

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
