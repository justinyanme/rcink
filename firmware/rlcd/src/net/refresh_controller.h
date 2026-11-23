#pragma once

#include <cstdint>

#include "net/fetch_diag.h"
#include "power/runtime_policy.h"
#include "render/link_status.h"
#include "sense/battery.h"
#include "store/nvs.h"
#include "render/usage.h"
#include "render/token.h"

namespace rcpaper {

struct RefreshAppState {
  DeviceState& device;
  BatteryReading& battery;
  RuntimeMode runtime_mode;
  uint64_t& retry_not_before_s;
  const char*& last_result;
  UsageSnapshot& usage;
  bool& usage_fetch_failed;
  TokenSnapshot& token;
  bool& token_fetch_failed;
  uint64_t& token_last_ok_s;
  uint64_t& token_retry_at_s;
  LinkFailure& link_failure;
  uint32_t& retry_cooldown_s;
  FetchDiag& usage_last_diag;
  FetchDiag& usage_last_err;
  FetchDiag& token_last_diag;
  FetchDiag& token_last_err;
};

bool refresh_due_now(const RefreshAppState& app);
bool start_network_refresh(RefreshAppState app, bool force_token = false);
bool finish_network_refresh(RefreshAppState app, bool& usage_page_changed,
                            bool& token_succeeded, bool& allowance_page_changed);
void adjust_refresh_gate_for_runtime_change(RefreshAppState app,
                                            RuntimeMode previous);

}  // namespace rcpaper
