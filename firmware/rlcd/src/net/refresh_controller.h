#pragma once

#include <cstdint>

#include "power/runtime_policy.h"
#include "sense/battery.h"
#include "store/nvs.h"

namespace rcpaper {

struct RefreshAppState {
  DeviceState& device;
  BatteryReading& battery;
  RuntimeMode runtime_mode;
  uint64_t& retry_not_before_s;
  const char*& last_result;
};

bool refresh_due_now(const RefreshAppState& app);
bool start_network_refresh(RefreshAppState app);
bool finish_network_refresh(RefreshAppState app);
void adjust_refresh_gate_for_runtime_change(RefreshAppState app,
                                            RuntimeMode previous);

}  // namespace rcpaper
