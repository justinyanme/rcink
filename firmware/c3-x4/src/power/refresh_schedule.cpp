// src/power/refresh_schedule.cpp
#include "power/refresh_schedule.h"

namespace rcpaper {

uint32_t schedule_next_refresh_ms(uint32_t now_ms, uint32_t interval_sec) {
  return now_ms + interval_sec * 1000UL;
}

bool refresh_due_ms(uint32_t now_ms, uint32_t deadline_ms) {
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

}  // namespace rcpaper
