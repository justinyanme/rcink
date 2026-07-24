#pragma once

#include <cstdint>

namespace rcpaper {

using WheelHandler = void (*)(int pin);

void wheel_init(WheelHandler tap_handler, WheelHandler long_press_handler);
void wheel_poll();

int wheel_wake_pin(bool ext1_wake, uint64_t ext1_mask);
uint64_t wheel_wake_mask();
void wheel_prepare_wake_inputs();

}  // namespace rcpaper
