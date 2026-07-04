// src/power/refresh_schedule.h
#pragma once
#include <cstdint>
#include <vector>
#include "proto/envelope.h"
#include "store/nvs.h"

namespace rcpaper {

uint32_t schedule_next_refresh_ms(uint32_t now_ms, uint32_t interval_sec);
bool refresh_due_ms(uint32_t now_ms, uint32_t deadline_ms);
uint32_t adaptive_refresh_interval_sec(uint32_t base_interval_sec,
                                       uint8_t unchanged_fetches,
                                       uint8_t battery_pct,
                                       bool battery_ok);
uint32_t pages_fingerprint(const std::vector<PageRecord>& pages);
void record_successful_fetch(DeviceState& state, const std::vector<PageRecord>& pages);

}  // namespace rcpaper
