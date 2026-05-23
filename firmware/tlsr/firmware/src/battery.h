#pragma once

#include "types.h"

extern u16 current_battery_mv;

// returns true if the battery reading changed
bool battery_try_update(void);
u16 battery_read_mv(void);
u8 mv_to_battery_level(u16 mv);

// clears adc setting flag (call after deep-sleep retention wake)
void _battery_clear_adc_setting_flag(void);
