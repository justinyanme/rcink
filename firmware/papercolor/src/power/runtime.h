#pragma once

namespace rcpaper {

// Re-arm after each fetch attempt; used by both awake and battery paths.
void schedule_runtime_refresh();
void runtime_begin();
void runtime_poll();
void print_power_state();

}  // namespace rcpaper
