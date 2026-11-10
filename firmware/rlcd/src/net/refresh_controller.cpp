#include "net/refresh_controller.h"

#include <Arduino.h>
#include <time.h>

#include <utility>

#include "config.h"
#include "net/refresh_flow.h"
#include "net/usage_config.h"
#include "power/network_policy.h"
#include "power/refresh_schedule.h"
#include "sense/clock.h"

namespace rcpaper {
namespace {

uint32_t battery_interval(const RefreshAppState& app) {
  return adaptive_refresh_interval_sec(
      FETCH_INTERVAL_SEC, app.device.unchanged_fetches, app.battery.pct,
      app.battery.ok);
}

uint32_t current_interval(const RefreshAppState& app) {
  return network_refresh_interval_sec(app.runtime_mode, battery_interval(app));
}

}  // namespace

bool refresh_due_now(const RefreshAppState& app) {
  const NetworkRefreshQuery query = {
      (uint64_t)time(nullptr),
      app.device.last_fetch_s,
      app.retry_not_before_s,
      app.device.pages.empty(),
      wall_clock_is_valid(),
      current_interval(app),
  };
  return network_refresh_due(query);
}

bool start_network_refresh(RefreshAppState app) {
  if (refresh_busy()) {
    Serial.println("[refresh] already running");
    return false;
  }
  if (refresh_start(app.device.last_fetch_fingerprint)) {
    Serial.println("[refresh] started");
    return true;
  }

  app.last_result = "refresh-task-failed";
  const uint32_t cooldown =
      network_failure_retry_sec(app.runtime_mode, battery_interval(app));
  app.retry_not_before_s = (uint64_t)time(nullptr) + cooldown;
  Serial.printf("[auto] retry gate %u s result=task-failed\n",
                (unsigned)cooldown);
  return false;
}

bool finish_network_refresh(RefreshAppState app) {
  RefreshCompletion completion;
  if (!refresh_take(completion)) return false;

  // The worker deliberately owns no board state. Reconcile on the Arduino loop
  // task so NVS, ADC, and RTC/I2C cannot race rendering or input.
  if (completion.clock_synced) write_rtc_from_clock();
  app.battery = read_battery();
#if RCRLCD_USAGE_ENABLED
  const bool success = fetch_succeeded(completion.result);
  if (success) app.usage = completion.usage;
  PageRecord page = usage_page(app.usage, completion.completed_at_s, !success);
  if (!app.device.pages.empty() && app.device.pages[0].layout_json == page.layout_json) {
    page.data_ts = app.device.pages[0].data_ts;
  }
  completion.pages = {std::move(page)};
  if (success) {
    record_successful_fetch(app.device, completion.pages);
    app.device.last_fetch_s = completion.completed_at_s;
  }
  app.device.pages = std::move(completion.pages);
  app.device.page_index = 0;
  // shortcut: source snapshots live only in RAM in #2; #5 adds validated persistence.
#else
  if (fetch_succeeded(completion.result)) {
    record_successful_fetch(app.device, completion.pages);
    app.device.pages = std::move(completion.pages);
    if (app.device.page_index >= app.device.pages.size()) {
      app.device.page_index = 0;
    }
    app.device.last_fetch_s = completion.completed_at_s;
    nvs_save(app.device);
    Serial.printf("[main] applied %u fetched pages idx=%u\n",
                  (unsigned)app.device.pages.size(),
                  (unsigned)app.device.page_index);
  }
#endif
  app.last_result = fetch_result_name(completion.result);

  const uint32_t adaptive = battery_interval(app);
  const uint32_t cooldown =
      fetch_succeeded(completion.result)
          ? network_refresh_interval_sec(app.runtime_mode, adaptive)
          : network_failure_retry_sec(app.runtime_mode, adaptive);
  app.retry_not_before_s = completion.completed_at_s + cooldown;
  Serial.printf("[auto] retry gate %u s result=%u\n", (unsigned)cooldown,
                (unsigned)completion.result);
  return true;
}

void adjust_refresh_gate_for_runtime_change(RefreshAppState app,
                                            RuntimeMode previous) {
  const uint64_t now = (uint64_t)time(nullptr);
  if (app.runtime_mode == RuntimeMode::Host) {
    // A real data host gets its own 15-minute policy, independent of an older
    // battery-mode cooldown.
    app.retry_not_before_s = 0;
  } else if (previous == RuntimeMode::Host &&
             app.retry_not_before_s > now) {
    const uint64_t battery_gate = now + battery_interval(app);
    if (app.retry_not_before_s < battery_gate) {
      app.retry_not_before_s = battery_gate;
    }
  }
}

}  // namespace rcpaper
