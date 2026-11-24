#include "net/refresh_controller.h"

#include <Arduino.h>
#include <time.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "config.h"
#include "net/refresh_flow.h"
#include "net/usage_config.h"
#include "power/network_policy.h"
#include "power/refresh_schedule.h"
#include "render/monument_persistence.h"
#include "render/monument_view.h"
#include "render/token_persistence.h"
#include "sense/clock.h"

namespace rcpaper {
namespace {

uint32_t battery_interval(const RefreshAppState& app) {
  return adaptive_refresh_interval_sec(
      FETCH_INTERVAL_SEC, app.device.unchanged_fetches, app.battery.pct,
      app.battery.ok);
}

uint32_t current_interval(const RefreshAppState& app) {
  return network_refresh_interval_sec(app.runtime_mode, battery_interval(app),
                                      RCRLCD_USAGE_ENABLED);
}

// HOST only: battery modes keep their adaptive schedule.
uint64_t early_refetch_at(const RefreshAppState& app) {
#if RCRLCD_USAGE_ENABLED
  if (app.runtime_mode != RuntimeMode::Host) return 0;
  return usage_refetch_at_s(app.usage, app.device.last_fetch_s);
#else
  (void)app;
  return 0;
#endif
}

}  // namespace

bool refresh_due_now(const RefreshAppState& app) {
  const uint64_t now = (uint64_t)time(nullptr);
  const NetworkRefreshQuery query = {
      now,
      app.device.last_fetch_s,
      app.retry_not_before_s,
      app.device.pages.empty(),
      wall_clock_is_valid(),
      current_interval(app),
      app.usage_fetch_failed,
  };
  if (network_refresh_due(query)) return true;
  const uint64_t early = early_refetch_at(app);
  if (early != 0 && query.clock_valid && now >= early && now >= app.retry_not_before_s) return true;
#if RCRLCD_USAGE_ENABLED
  // Token cadence never starts a worker; it rides the next Allowance run.
  // Only an explicit BOOT request that missed the active worker is the exception.
  return refresh_token_pending();
#else
  return false;
#endif
}

bool start_network_refresh(RefreshAppState app, bool force_token) {
  const bool token_due = RCRLCD_USAGE_ENABLED &&
      (force_token ||
       (app.monument.end_date.empty() && !app.token_fetch_failed) ||
       token_refresh_due(
           (uint64_t)time(nullptr), app.token_last_ok_s, app.token_retry_at_s,
           app.runtime_mode, app.token_fetch_failed));
  if (refresh_busy()) {
    refresh_start(app.device.last_fetch_fingerprint, token_due);  // join if possible
    Serial.println("[refresh] already running");
    return false;
  }
  if (refresh_start(app.device.last_fetch_fingerprint, token_due)) {
    Serial.println("[refresh] started");
    return true;
  }

  app.last_result = "refresh-task-failed";
  const uint32_t cooldown =
      network_failure_retry_sec(app.runtime_mode, battery_interval(app),
                                RCRLCD_USAGE_ENABLED);
  app.retry_not_before_s = (uint64_t)time(nullptr) + cooldown;
  app.retry_cooldown_s = cooldown;
  Serial.printf("[auto] retry gate %u s result=task-failed\n",
                (unsigned)cooldown);
  return false;
}

bool finish_network_refresh(RefreshAppState app, bool& usage_page_changed,
                            bool& token_succeeded, bool& allowance_page_changed,
                            bool& monument_succeeded) {
  usage_page_changed = false;
  token_succeeded = false;
  allowance_page_changed = false;
  monument_succeeded = false;
  std::unique_ptr<RefreshCompletion> completion_ptr(new RefreshCompletion());
  if (!refresh_take(*completion_ptr)) return false;
  RefreshCompletion& completion = *completion_ptr;

  // The worker deliberately owns no board state. Reconcile on the Arduino loop
  // task so NVS, ADC, and RTC/I2C cannot race rendering or input.
  if (completion.clock_synced) write_rtc_from_clock();
  app.link_failure = completion.link_failure;
  app.battery = read_battery();
  const bool success = fetch_succeeded(completion.result);
#if RCRLCD_USAGE_ENABLED
  if (completion.usage_diag.attempted) {
    completion.usage_diag.at_s = completion.completed_at_s;
    app.usage_last_diag = completion.usage_diag;
    if (!completion.usage_diag.ok) app.usage_last_err = completion.usage_diag;
  }
  app.usage_fetch_failed = !success;
  if (success) {
    merge_usage(completion.usage, app.usage);
    const std::string saved = encode_usage(app.usage);
    // Retry a failed commit on later successful polls without rewriting duplicates.
    if (app.usage.served_at != 0 && nvs_load_usage() != saved && !nvs_save_usage(saved)) {
      Serial.println("[usage] cache save failed; retained in memory only");
    }
    // Adaptive backoff follows source observations, not changing age labels.
    std::vector<PageRecord> observations;
    for (size_t i = 0; i < app.usage.count; i++) {
      PageRecord record;
      record.id = std::to_string(i);
      record.data_ts = app.usage.providers[i].fetched_at;
      observations.push_back(std::move(record));
    }
    record_successful_fetch(app.device, observations);
    app.device.last_fetch_s = completion.completed_at_s;
  }
  if (completion.token_diag.attempted) {
    completion.token_diag.at_s = completion.completed_at_s;
    app.token_last_diag = completion.token_diag;
    if (!completion.token_diag.ok) app.token_last_err = completion.token_diag;
  }
  if (completion.token_attempted) {
    app.token_fetch_failed = !completion.token_ok;
    app.token_retry_at_s = completion.token_ok ? 0 : completion.completed_at_s +
        (app.runtime_mode == RuntimeMode::Host
            ? kUsageHostFailureRetrySec : kTokenBatteryRefreshIntervalSec);
    if (completion.token_ok) {
      app.token = completion.token;
      app.token_last_ok_s = completion.completed_at_s;
      token_succeeded = true;
      if (!persist_token_snapshot(app.token)) {
        Serial.println("[token] cache save failed; retained in memory only");
      }
    }
    if (completion.monument_ok) {
      app.monument = completion.monument;
      monument_succeeded = true;
      if (!persist_monument_snapshot(app.monument)) {
        Serial.println("[monument] cache save failed; retained in memory only");
      }
    }
  }
#else
  if (success) {
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
  const uint32_t cooldown = RCRLCD_USAGE_ENABLED
      ? allowance_refresh_cooldown_sec(app.runtime_mode, adaptive, success)
      : (success ? network_refresh_interval_sec(app.runtime_mode, adaptive, false)
                 : network_failure_retry_sec(app.runtime_mode, adaptive, false));
  uint64_t gate = completion.completed_at_s + cooldown;
#if RCRLCD_USAGE_ENABLED
  if (success) {  // let an expiring or late sample pull the next fetch forward
    const uint64_t early = early_refetch_at(app);
    const uint64_t soonest = completion.completed_at_s + kUsageChaseRetryS;
    if (early != 0 && early < gate) gate = early > soonest ? early : soonest;
  }
#endif
  app.retry_not_before_s = gate;
  app.retry_cooldown_s = (uint32_t)(gate - completion.completed_at_s);
#if RCRLCD_USAGE_ENABLED
  // After the gate is known, so a failure can say when it will retry.
  const uint64_t token_retry = app.link_failure != LinkFailure::None
      ? app.retry_not_before_s
      : std::max(app.token_retry_at_s, app.retry_not_before_s);
  allowance_page_changed = update_usage_page(
      app.device, app.usage, completion.completed_at_s,
      {!success, app.device.last_fetch_s, app.retry_not_before_s, false,
       app.link_failure, app.retry_cooldown_s, WIFI_SSID});
  const bool token_changed = update_token_page(
      app.device, app.token, completion.completed_at_s,
      {app.token_fetch_failed, app.token_last_ok_s, token_retry, false,
       app.link_failure, app.retry_cooldown_s, WIFI_SSID});
  const bool monument_changed = update_monument_page(
      app.device, app.monument, app.token, completion.completed_at_s,
      {app.token_fetch_failed, app.token_last_ok_s, token_retry, false,
       app.link_failure, app.retry_cooldown_s, WIFI_SSID},
      app.runtime_mode, app.heatmap_window);
  usage_page_changed = allowance_page_changed || token_changed || monument_changed;
#endif
  Serial.printf("[auto] retry gate %u s result=%u link=%u\n",
                (unsigned)(gate - completion.completed_at_s),
                (unsigned)completion.result,
                (unsigned)app.link_failure);
  return true;
}

void adjust_refresh_gate_for_runtime_change(RefreshAppState app,
                                            RuntimeMode previous) {
  const uint64_t now = (uint64_t)time(nullptr);
#if RCRLCD_USAGE_ENABLED
  if (app.token_fetch_failed) {
    if (app.runtime_mode == RuntimeMode::Host) {
      app.token_retry_at_s = token_retry_on_host_entry(app.token_retry_at_s, true);
    } else if (previous == RuntimeMode::Host) {
      app.token_retry_at_s = now + kTokenBatteryRefreshIntervalSec;
    }
  }
#endif
  if (app.runtime_mode == RuntimeMode::Host) {
    // A real data host gets its own fixed policy, independent of an older
    // battery-mode cooldown.
    app.retry_not_before_s = 0;
    app.retry_cooldown_s = 0;
  } else if (previous == RuntimeMode::Host &&
             app.retry_not_before_s > now) {
    const uint32_t adaptive = battery_interval(app);
    const uint64_t battery_gate = now + adaptive;
    if (app.retry_not_before_s < battery_gate) {
      app.retry_not_before_s = battery_gate;
      app.retry_cooldown_s = adaptive;
    }
  }
}

}  // namespace rcpaper
