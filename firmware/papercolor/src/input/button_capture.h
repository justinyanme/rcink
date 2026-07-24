#pragma once

#include <stdint.h>

namespace rcpaper {

struct ButtonPressCounts {
  uint32_t a = 0;
  uint32_t b = 0;
  uint32_t c = 0;
};

void install_button_isrs();
uint64_t sample_button_low_mask();
ButtonPressCounts button_press_counts();

}  // namespace rcpaper
