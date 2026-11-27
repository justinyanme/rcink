#include <unity.h>

#include <ctime>
#include <functional>
#include "render/neon_view.h"

namespace {
constexpr time_t noon = 1795752000;  // 2026-11-27 12:00 SGT
time_t now;
time_t fixture_time(time_t*) { return now; }
}

// Run the real worker and controller with a stepped clock and local I/O.
#define time fixture_time
#include "../../src/net/refresh_controller.cpp"
#include "../../src/net/refresh_flow.cpp"
#undef time

using namespace rcpaper;

namespace {
enum class Call { Link, Allowance, Token, History, Disconnect };
std::function<void(Call)> on_call;
void (*worker)(void*);
bool create_ok, wifi_ok, clock_ok, history_ok;
unsigned history_requests;
}

BaseType_t xTaskCreate(void (*task)(void*), const char*, unsigned, void*, UBaseType_t, void*) {
  if (!create_ok) return 0;
  worker = task;
  return pdPASS;
}

namespace rcpaper {
bool wifi_connect(int) { on_call(Call::Link); return wifi_ok; }
void wifi_disconnect() { on_call(Call::Disconnect); }
bool ntp_sync_once(const char*) { return clock_ok; }
bool wall_clock_is_valid() { return clock_ok; }
bool usage_fetch(UsageSnapshot&, FetchDiag*) { on_call(Call::Allowance); return true; }
bool token_fetch(TokenSnapshot& token, FetchDiag*) {
  on_call(Call::Token);
  token.today = "2026-11-27";
  token.required_source_collected_at = now;
  return true;
}
bool monument_fetch(MonumentSnapshot& history, FetchDiag*) {
  ++history_requests;
  on_call(Call::History);
  history.end_date = "2026-11-27";
  return history_ok;
}
BatteryReading read_battery() { return {true, 3800, 80}; }
void write_rtc_from_clock() {}
std::string nvs_load_usage() { return {}; }
bool nvs_save_usage(const std::string&) { return true; }
std::string nvs_load_token() { return {}; }
bool nvs_save_token(const std::string&) { return true; }
std::string nvs_load_monument() { return {}; }
bool nvs_save_monument(const std::string&) { return true; }
}

namespace {
struct Desk {
  DeviceState device;
  BatteryReading battery = read_battery();
  const char* result = "";
  UsageSnapshot usage{};
  TokenSnapshot token{};
  MonumentSnapshot history{};
  bool usage_failed = false, token_failed = false, history_failed = false;
  uint64_t gate = 0, token_ok = 0, token_retry = 0;
  uint32_t cooldown = 0;
  LinkFailure link{};
  FetchDiag diag{};
  RefreshAppState app{device, battery, RuntimeMode::Host, gate, result, usage,
                      usage_failed, token, token_failed, token_ok, token_retry, history,
                      history_failed, HeatmapWindow::Year, HeatmapWindow::SixMonths,
                      link, cooldown, diag, diag, diag, diag};
  Desk() {
    refresh_flow_init();
    now = noon;
    create_ok = wifi_ok = clock_ok = true;
    history_ok = false;
    history_requests = 0;
    on_call = [](Call){};
    history.end_date = "2026-11-26";
  }
  void start() { TEST_ASSERT_TRUE(start_network_refresh(app)); }
  void finish() {
    TEST_ASSERT_NOT_NULL(worker);
    worker(nullptr);
    worker = nullptr;
    bool changed, token_success, allowance_changed, history_success;
    TEST_ASSERT_TRUE(finish_network_refresh(app, changed, token_success,
                                            allowance_changed, history_success));
    TEST_ASSERT_FALSE(refresh_busy());
  }
  void footer(const char* chip, const char* text, bool manual = false) {
    UsageSync u{usage_failed, device.last_fetch_s, gate, manual, link, cooldown, "test"};
    TokenSync t{token_failed, token_ok, std::max(token_retry, gate), manual, link, cooldown, "test"};
    const auto v = derive_neon_view(usage, u, token, history, t, monument_retry(app, now),
                                    now, {}, battery, true);
    TEST_ASSERT_EQUAL_STRING(chip, v.footer_chip.c_str());
    TEST_ASSERT_EQUAL_STRING(text, v.footer_text.c_str());
  }
};
}

void test_monument_retry_after_allowance_only_poll_and_host_entry() {
  Desk d;
  d.app.runtime_mode = RuntimeMode::Active;
  d.start();
  d.finish();  // Token succeeds, annual history fails
  TEST_ASSERT_TRUE(d.history_failed);
  d.footer("TOKEN FAIL", "next sync");  // next battery worker is Allowance-only
  now = noon + 1800;
  TEST_ASSERT_TRUE(refresh_due_now(d.app, now));
  d.start();
  d.finish();
  TEST_ASSERT_EQUAL(1, history_requests);
  now = noon + 31*60;
  d.app.runtime_mode = RuntimeMode::Host;
  adjust_refresh_gate_for_runtime_change(d.app, RuntimeMode::Active);
  d.footer("TOKEN FAIL", "retry 12:35");
  TEST_ASSERT_FALSE(refresh_due_now(d.app, noon+35*60-1));
  TEST_ASSERT_TRUE(refresh_due_now(d.app, noon+35*60));
  d.usage.count = 1;
  d.usage.providers[0] = {"codex", UsageStatus::Ok, 30, (uint64_t)now, noon+34*60, 0};
  d.footer("TOKEN FAIL", "retry 12:33");
  TEST_ASSERT_TRUE(refresh_due_now(d.app, noon+33*60));
}

void test_allowance_only_worker_crosses_history_deadline() {
  Desk d;
  d.start();
  d.finish();
  now = noon + 9*60 + 55;
  d.start();
  d.finish();
  now += 300;  // 12:14:55, five seconds before Token/history is eligible
  TEST_ASSERT_TRUE(refresh_due_now(d.app, now));
  d.start();
  on_call = [&](Call call) {
    if (call == Call::Allowance) {
      now = noon + 15*60;
      d.footer("TOKEN FAIL", "next sync");
    }
  };
  d.finish();
  TEST_ASSERT_EQUAL(1, history_requests);  // only the noon GET
  d.footer("TOKEN FAIL", "retry 12:20");
  now += 300;
  d.footer("TOKEN FAIL", "next sync");  // eligible, still idle
  TEST_ASSERT_TRUE(refresh_due_now(d.app, now));
  d.start();
  history_ok = true;
  on_call = [&](Call call) {
    if (call == Call::Disconnect) d.footer("TOKEN FAIL", "next sync");
    else d.footer("TOKEN FAIL", "retrying");  // link through the annual GET
  };
  d.finish();
  TEST_ASSERT_EQUAL(2, history_requests);
  d.footer("", "");
}

void test_boot_joins_or_queues_history_and_clears_on_failure() {
  Desk d;
  d.start();
  d.finish();
  now += 300;
  d.start();  // Allowance only until BOOT joins during Wi-Fi association
  on_call = [&](Call call) {
    if (call == Call::Link) {
      TEST_ASSERT_FALSE(start_network_refresh(d.app, true));
      d.footer("SYNCING", "fetching now", true);
    } else if (call == Call::History) {
      d.footer("TOKEN FAIL", "retrying");
    }
  };
  d.finish();
  TEST_ASSERT_EQUAL(2, history_requests);
  now += 300;
  d.start();
  on_call = [&](Call call) {
    if (call == Call::Disconnect) {  // this run already skipped the Token stage
      TEST_ASSERT_FALSE(start_network_refresh(d.app, true));  // late BOOT
      d.footer("SYNCING", "next sync", true);
    }
  };
  d.finish();
  TEST_ASSERT_EQUAL(2, history_requests);
  TEST_ASSERT_TRUE(refresh_token_pending());
  TEST_ASSERT_TRUE(refresh_due_now(d.app, now));
  d.footer("SYNCING", "next sync", true);
  d.start();  // queued request survives into the next run
  wifi_ok = false;
  on_call = [&](Call call) {
    if (call == Call::Link) d.footer("SYNCING", "fetching now", true);
    else d.footer("TOKEN FAIL", "next sync", true);  // link failed before GET
  };
  d.finish();
  TEST_ASSERT_FALSE(refresh_token_pending());
  d.footer("NO WIFI", "retry 12:11", true);
  TEST_ASSERT_EQUAL(2, history_requests);
  now += 60;
  wifi_ok = true;
  clock_ok = false;
  on_call = [](Call){};
  d.start();
  d.finish();
  d.footer("NO NTP", "retry 12:12");
  create_ok = false;
  TEST_ASSERT_FALSE(start_network_refresh(d.app, true));
  TEST_ASSERT_FALSE(refresh_busy());
  TEST_ASSERT_FALSE(refresh_token_pending());
  d.footer("NO NTP", "retry 12:12", true);
}

void test_boot_during_history_get_reports_the_followup() {
  Desk d;
  d.start();
  on_call = [&](Call call) {
    if (call == Call::History) {
      TEST_ASSERT_FALSE(start_network_refresh(d.app, true));  // BOOT after the Token stage began
      d.footer("SYNCING", "next sync", true);
    }
  };
  d.finish();
  d.footer("SYNCING", "next sync", true);

  d.start();
  on_call = [&](Call call) {
    if (call == Call::History) d.footer("SYNCING", "fetching now", true);
  };
  d.finish();
  TEST_ASSERT_EQUAL(2, history_requests);
  TEST_ASSERT_FALSE(refresh_token_pending());
}
