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
#include "net/usage_config.h"
#include "net/ntp.h"
#include "net/wifi.h"
#include "power/refresh_schedule.h"

namespace rcpaper {
namespace {

constexpr uint32_t kWorkerStackBytes = 8192;
constexpr UBaseType_t kWorkerPriority = 1;

SemaphoreHandle_t g_mutex = nullptr;
bool g_busy = false;
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

RefreshCompletion run_refresh(uint32_t previous_fingerprint) {
  RefreshCompletion completion;
  if (!wifi_connect()) {
    Serial.println("[main] wifi failed");
    completion.result = FetchResult::WifiFailed;
  } else {
    completion.clock_synced = ntp_sync_once(LOCAL_TZ);
#if RCRLCD_USAGE_ENABLED
    completion.result = usage_fetch(completion.usage) ? FetchResult::Changed : FetchResult::DataFailed;
#else
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

bool refresh_start(uint32_t previous_fingerprint) {
  lock();
  if (g_busy) {
    unlock();
    return false;
  }
  g_busy = true;
  g_previous_fingerprint = previous_fingerprint;
  unlock();

  const BaseType_t created =
      xTaskCreate(refresh_worker, "rlcd-refresh", kWorkerStackBytes, nullptr,
                  kWorkerPriority, nullptr);
  if (created == pdPASS) return true;

  lock();
  g_busy = false;
  unlock();
  Serial.printf("[refresh] task creation FAILED: code=%ld\n", (long)created);
  return false;
}

bool refresh_busy() {
  lock();
  const bool busy = g_busy;
  unlock();
  return busy;
}

bool refresh_take(RefreshCompletion& out) {
  require_initialized();
  if (xSemaphoreTake(g_mutex, 0) != pdTRUE) return false;
  if (!g_completion_ready) {
    unlock();
    return false;
  }
  out = std::move(g_completion);
  g_completion = RefreshCompletion{};
  g_completion_ready = false;
  g_busy = false;
  unlock();
  return true;
}

}  // namespace rcpaper
