#include <unity.h>

#include "render/neon_view.h"
#include "../../src/net/refresh_controller.cpp"

using namespace rcpaper;

namespace {
RefreshCompletion completion;
bool pending;
}

// Use the production completion, runtime transition and due checks with local I/O.
namespace rcpaper {
bool refresh_take(RefreshCompletion& out) { out = completion; return true; }
bool refresh_busy() { return false; }
bool refresh_start(uint32_t, bool) { return true; }
bool refresh_token_pending() { return pending; }
BatteryReading read_battery() { return {true, 3800, 80}; }
void write_rtc_from_clock() {}
std::string nvs_load_usage() { return {}; }
bool nvs_save_usage(const std::string&) { return true; }
std::string nvs_load_token() { return {}; }
bool nvs_save_token(const std::string&) { return true; }
std::string nvs_load_monument() { return {}; }
bool nvs_save_monument(const std::string&) { return true; }
}

void test_monument_retry_after_allowance_only_poll_and_host_entry() {
  constexpr uint64_t noon = 1795752000;  // 2026-11-27 12:00 SGT
  DeviceState device;
  BatteryReading battery = read_battery();
  const char* result = "";
  UsageSnapshot usage{};
  TokenSnapshot token{};
  MonumentSnapshot history{};
  history.end_date = "2026-11-26";
  history.days.assign(kMonumentDays, 1);
  bool usage_failed = false, token_failed = false, history_failed = false;
  uint64_t gate = 0, token_ok = 0, token_retry = 0;
  uint32_t cooldown = 0;
  LinkFailure link{};
  FetchDiag diag{};
  RefreshAppState app{device, battery, RuntimeMode::Active, gate, result, usage,
                      usage_failed, token, token_failed, token_ok, token_retry, history,
                      history_failed, HeatmapWindow::Year, HeatmapWindow::SixMonths,
                      link, cooldown, diag, diag, diag, diag};
  auto complete = [&](uint64_t at, bool fetch_token) {
    completion = {};
    completion.result = FetchResult::Changed;
    completion.completed_at_s = at;
    completion.token_attempted = completion.token_ok = fetch_token;
    completion.token.today = "2026-11-27";
    completion.token.required_source_collected_at = at;
    bool changed, token_success, allowance_changed, history_success;
    TEST_ASSERT_TRUE(finish_network_refresh(app, changed, token_success,
                                            allowance_changed, history_success));
    TEST_ASSERT_FALSE(history_success);
    TEST_ASSERT_TRUE(history_failed);
  };
  complete(noon, true);  // Token succeeds, annual history fails
  TEST_ASSERT_EQUAL_UINT64(noon+3600, monument_retry_at(app, noon).value());
  TEST_ASSERT_TRUE(refresh_due_now(app, noon+1800));
  complete(noon+1800, false);  // Allowance-only success at 12:30
  app.runtime_mode = RuntimeMode::Host;
  adjust_refresh_gate_for_runtime_change(app, RuntimeMode::Active);
  const auto at = noon+31*60;
  const auto retry = monument_retry_at(app, at);
  TEST_ASSERT_EQUAL_UINT64(noon+35*60, retry.value());
  const auto view = derive_neon_view(usage, {}, token, history, {}, retry, at,
                                     {}, battery, true);
  TEST_ASSERT_EQUAL_STRING("TOKEN FAIL", view.footer_chip.c_str());
  TEST_ASSERT_EQUAL_STRING("retry 12:35", view.footer_text.c_str());
  TEST_ASSERT_FALSE(refresh_due_now(app, *retry-1));
  TEST_ASSERT_TRUE(refresh_due_now(app, *retry));

  // An expiring Allowance sample brings the same worker and its footer forward.
  usage.count = 1;
  usage.providers[0] = {"codex", UsageStatus::Ok, 30, at, noon+34*60, 0};
  TEST_ASSERT_EQUAL_UINT64(noon+33*60, monument_retry_at(app, at).value());
  TEST_ASSERT_TRUE(refresh_due_now(app, noon+33*60));
  pending = true;
  TEST_ASSERT_EQUAL_UINT64(at, monument_retry_at(app, at).value());
  TEST_ASSERT_TRUE(refresh_due_now(app, at));
  pending = false;
}
