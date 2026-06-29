// src/power/refresh_schedule.h
#pragma once
#include <cstdint>

namespace rcpaper {

uint32_t schedule_next_refresh_ms(uint32_t now_ms, uint32_t interval_sec);
bool refresh_due_ms(uint32_t now_ms, uint32_t deadline_ms);

}  // namespace rcpaper
