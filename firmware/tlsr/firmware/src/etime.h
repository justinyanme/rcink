#pragma once

#include <stdint.h>
#include "types.h"
#include "app_config.h"

typedef enum {
    TIMER_C_EPD_UPDATE = 0,
    TIMER_C_LED = 1,
    TIMER_C_AIR_TAG = 2,

    TIMER_C_LENGTH,
} TIMER_CHANNEL;

#ifdef DEBUG
#define TIMER_EPD_UPDATE_PERIOD_SECONDS 15
#define TIMER_LED_PERIOD_SECONDS 5
#define TIMER_AIR_TAG_PERIOD_SECONDS 7
#else
#define TIMER_EPD_UPDATE_PERIOD_SECONDS 30
#define TIMER_LED_PERIOD_SECONDS 1
#define TIMER_AIR_TAG_PERIOD_SECONDS 17
#endif

const static u16 TIMER_CHANNEL_PERIOD_MAP[TIMER_C_LENGTH] = {
    TIMER_EPD_UPDATE_PERIOD_SECONDS,
    TIMER_LED_PERIOD_SECONDS,
    TIMER_AIR_TAG_PERIOD_SECONDS,
};

struct date_time {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_day;
    int tm_month;
    int tm_year;
    int tm_week; // 0~6, monday is 0
    int tm_tz_offset;
};

void init_time(void);
void handle_time(void);
bool check_time_period_reached(TIMER_CHANNEL ch);
void set_time(uint32_t new_ts, uint16_t time_year, uint8_t time_month, uint8_t time_day, uint8_t time_week);
struct date_time get_time(void);
