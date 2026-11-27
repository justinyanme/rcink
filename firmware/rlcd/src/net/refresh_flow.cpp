#include "net/refresh_flow.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <time.h>

#include <cstdlib>
#include <utility>
#include <vector>

#include "config.h"
#include "net/axiom.h"
#include "net/usage_client.h"
#include "net/token_client.h"
#include "net/usage_config.h"
#include "net/ntp.h"
#include "net/wifi.h"
#include "power/refresh_schedule.h"
#include "sense/clock.h"

namespace rcpaper {
namespace {

// The completion carries a second 30-day snapshot and 365-day monument while
// TLS/HTTP are active.
constexpr uint32_t kWorkerStackBytes = 16384;
constexpr UBaseType_t kWorkerPriority = 1;

SemaphoreHandle_t g_mutex = nullptr;
enum class WorkerPhase { Idle, BeforeToken, FetchingToken, Finishing };
WorkerPhase g_phase = WorkerPhase::Idle;
bool g_token_due = false;
bool g_completion_ready = false;
uint32_t g_previous_fingerprint = 0;
RefreshCompletion g_completion;

[[noreturn]] void fatal(const char* message) {
  Serial.printf("[refresh] FATAL: %s\n", message);
  Serial.flush();
  abort();
}

void require_initialized() {
  if (g_mutex == nullptr) fatal("refresh_flow_init() was not called");
}

void lock() {
  require_initialized();
  if (xSemaphoreTake(g_mutex, portMAX_DELAY) != pdTRUE) {
    fatal("mutex take failed");
  }
}

void unlock() {
  if (xSemaphoreGive(g_mutex) != pdTRUE) fatal("mutex give failed");
}

bool consume_token_due(bool link_ready) {
  lock();
  const bool due = g_token_due;
  g_token_due = false;
  g_phase = due && link_ready ? WorkerPhase::FetchingToken : WorkerPhase::Finishing;
  unlock();
  return due;
}

RefreshCompletion run_refresh(uint32_t previous_fingerprint) {
  RefreshCompletion completion;
  if (!wifi_connect()) {
    Serial.println("[main] wifi failed");
    completion.result = FetchResult::WifiFailed;
    completion.link_failure = LinkFailure::WifiFailed;
    completion.token_attempted = consume_token_due(false);
  } else {
    completion.clock_synced = ntp_sync_once(LOCAL_TZ);
#if RCRLCD_USAGE_ENABLED
    if (!wall_clock_is_valid()) {
      Serial.println("[main] clock still invalid after NTP; skipping HTTPS");
      completion.result = FetchResult::DataFailed;
      completion.link_failure = LinkFailure::NtpFailed;
      completion.token_attempted = consume_token_due(false);
    } else {
      completion.link_failure = LinkFailure::None;
      completion.result = usage_fetch(completion.usage, &completion.usage_diag)
          ? FetchResult::Changed : FetchResult::DataFailed;
      completion.token_attempted = consume_token_due(true);
      if (completion.token_attempted) {
        completion.token_ok = token_fetch(completion.token, &completion.token_diag);
        completion.monument_ok =
            monument_fetch(completion.monument, &completion.monument_diag);
        lock();
        g_phase = WorkerPhase::Finishing;
        unlock();
      }
    }
#else
    completion.link_failure = LinkFailure::None;
    std::vector<PageRecord> fresh;
    if (!axiom_fetch_pages(fresh) || fresh.empty()) {
      Serial.println("[main] fetch failed; keeping cache");
      completion.result = FetchResult::DataFailed;
    } else {
      const uint32_t fresh_fingerprint = pages_fingerprint(fresh);
      completion.result =
          previous_fingerprint == 0 ||
                  previous_fingerprint != fresh_fingerprint
              ? FetchResult::Changed
              : FetchResult::Unchanged;
      completion.pages = std::move(fresh);
      Serial.printf("[main] fetched %u pages changed=%d\n",
                    (unsigned)completion.pages.size(),
                    (int)(completion.result == FetchResult::Changed));
    }
#endif
  }
  wifi_disconnect();
  completion.completed_at_s = (uint64_t)time(nullptr);
  return completion;
}

void refresh_worker(void*) {
  {
    lock();
    const uint32_t previous_fingerprint = g_previous_fingerprint;
    unlock();
    RefreshCompletion completion = run_refresh(previous_fingerprint);
    lock();
    g_completion = std::move(completion);
    g_completion_ready = true;
    unlock();
  }  // Destroy moved-from C++ owners before FreeRTOS deletes this task stack.
  vTaskDelete(nullptr);
}

}  // namespace

void refresh_flow_init() {
  if (g_mutex != nullptr) return;
  g_mutex = xSemaphoreCreateMutex();
  if (g_mutex == nullptr) fatal("mutex allocation failed");
}

bool refresh_start(uint32_t previous_fingerprint, bool token_due) {
  lock();
  // A BOOT during an Allowance-only run can still join before its second GET.
  // If that stage already passed, the pending bit triggers an immediate run.
  if (token_due) g_token_due = true;
  if (g_phase != WorkerPhase::Idle) {
    unlock();
    return false;
  }
  g_phase = WorkerPhase::BeforeToken;
  g_previous_fingerprint = previous_fingerprint;
  unlock();

  const BaseType_t created =
      xTaskCreate(refresh_worker, "rlcd-refresh", kWorkerStackBytes, nullptr,
                  kWorkerPriority, nullptr);
  if (created == pdPASS) return true;

  lock();
  g_phase = WorkerPhase::Idle;
  g_token_due = false;  // the ordinary failure gate must not be bypassed forever
  unlock();
  Serial.printf("[refresh] task creation FAILED: code=%ld\n", (long)created);
  return false;
}

RefreshFeeds refresh_feeds() {
  lock();
  TokenWork work = TokenWork::None;
  if (g_phase == WorkerPhase::FetchingToken || (g_phase == WorkerPhase::BeforeToken && g_token_due))
    work = TokenWork::InFlight;
  else if (g_token_due)
    work = TokenWork::Queued;
  const RefreshFeeds feeds{g_phase != WorkerPhase::Idle, g_token_due, work};
  unlock();
  return feeds;
}

bool refresh_take(RefreshCompletion& out) {
  require_initialized();
  if (xSemaphoreTake(g_mutex, 0) != pdTRUE) return false;
  if (!g_completion_ready) {
    unlock();
    return false;
  }
  out = std::move(g_completion);
  g_completion.pages.clear();
  g_completion.monument.days.clear();
  g_completion.result = FetchResult::DataFailed;
  g_completion.link_failure = LinkFailure::None;
  g_completion.token_attempted = false;
  g_completion.token_ok = false;
  g_completion.monument_ok = false;
  g_completion.clock_synced = false;
  g_completion.completed_at_s = 0;
  g_completion_ready = false;
  g_phase = WorkerPhase::Idle;  // keep a late BOOT for the next run
  unlock();
  return true;
}

}  // namespace rcpaper
