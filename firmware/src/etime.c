#include "drivers.h"
#include "etime.h"

#include <utility.h>
#include <epd/epd.h>

#include "tlkapi_debug.h"
#include "log.h"

#include "app_config.h"
#include "time_config.h"

RAM struct date_time current_date = {0};

// The higher the number, the faster the clock runs (drift compensation).
RAM u32 time_offset_ticks = 5000;

RAM u32 last_clock_tick = 0;
RAM u32 current_unix_time;
RAM u32 one_second_trimmed = CLOCK_16M_SYS_TIMER_CLK_1S;

uint8_t month_days_map[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// for timer channel
RAM u32 timer_c_last_ts[TIMER_C_LENGTH] = {0};
RAM bool timer_c_ever_reached[TIMER_C_LENGTH] = {FALSE};

_attribute_ram_code_ void _update_current_date() {
    const u8 pre_hour = current_date.tm_hour;

    const u32 ts = current_unix_time + current_date.tm_tz_offset;
    current_date.tm_min = (ts / 60) % 60;
    current_date.tm_hour = ((ts / 60) / 60) % 24;
    current_date.tm_sec = ts % 60;

    if (current_date.tm_hour >= pre_hour) {
        return;
    }

    // hours changed means its a new day
    if (current_date.tm_day + 1 > month_days_map[current_date.tm_month - 1]) {
        current_date.tm_day = 1;
        if (current_date.tm_month + 1 > 12) {
            current_date.tm_month = 1;
            current_date.tm_year += 1;
        } else {
            current_date.tm_month += 1;
        }
    } else {
        current_date.tm_day = current_date.tm_day + 1;
    }
    current_date.tm_week = (current_date.tm_week + 1) % 7;
}

_attribute_ram_code_ void init_time() {
    current_unix_time = BUILD_UNIX_TIMESTAMP;
    one_second_trimmed -= time_offset_ticks;
    current_date.tm_tz_offset = 3600 * 8;

    current_date.tm_year = BUILD_YEAR;
    current_date.tm_month = BUILD_MONTH;
    // cmake: The day of the current week. 0 is Sunday. (0-6)
    // convert to 0~6, 0 is Monday
    current_date.tm_week = BUILD_WEEK_DAY > 0 ? BUILD_WEEK_DAY - 1 : 6;
    current_date.tm_day = BUILD_DAY;
    current_date.tm_hour = BUILD_HOUR;
    current_date.tm_min = BUILD_MIN;
    current_date.tm_sec = BUILD_SEC;
}


_attribute_ram_code_ void handle_time() {
    // skip if less than 1s
    const u32 new_clock_tick = clock_time();
    const u32 delta_tick = new_clock_tick - last_clock_tick;
    if (delta_tick < one_second_trimmed) {
        // log_verbosef("skip, last_clock_tick: %d, clock_time: %d", last_clock_tick, clock_time());
        return;
    }

    current_unix_time += delta_tick / one_second_trimmed;
    log_verbosef("%d -> %d, delta: %d, ts: %d", last_clock_tick, new_clock_tick, delta_tick, current_unix_time);

    last_clock_tick = new_clock_tick;
    _update_current_date();
}

// return true if reached time period or check first time
_attribute_ram_code_ bool check_time_period_reached(TIMER_CHANNEL ch) {
    const u16 seconds = TIMER_CHANNEL_PERIOD_MAP[ch];
    // 0 means never reached -> never execute the logic
    if (seconds == 0) {
        return FALSE;
    }
    if (!timer_c_ever_reached[ch]) {
        timer_c_ever_reached[ch] = TRUE;
        return TRUE;
    }
    if (current_unix_time - timer_c_last_ts[ch] >= seconds) {
        timer_c_last_ts[ch] = current_unix_time;
        return TRUE;
    }
    return FALSE;
}

_attribute_ram_code_ void set_time(
    uint32_t new_ts, uint16_t time_year, uint8_t time_month, uint8_t time_day, uint8_t time_week) {
    log_debugf("set_time: %d, %d-%d-%d, %d", new_ts, time_year, time_month, time_day, time_week);

    const bool needs_mark_force_update = abs(new_ts - current_unix_time) > 3600;

    last_clock_tick = clock_time();
    current_unix_time = new_ts;

    current_date.tm_year = time_year;
    current_date.tm_month = time_month;
    current_date.tm_day = time_day;
    current_date.tm_week = time_week;

    _update_current_date();

    if (needs_mark_force_update) {
        set_force_refresh_when_available(EPD_RM_FULL);
    }
}

_attribute_ram_code_ struct date_time get_time() {
    return current_date;
}
