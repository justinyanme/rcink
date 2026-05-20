#pragma once

#include <OneBitDisplay.h>

#include "types.h"


void EPD_Display(unsigned char *image, unsigned char *red_image, bool full_refresh);
void FixBuffer();

// display with scene
#ifndef EPD_HAS_SCENE_TIME
#define EPD_HAS_SCENE_TIME 1
#endif

#ifndef EPD_HAS_SCENE_LIST
#define EPD_HAS_SCENE_LIST 1
#endif

#if EPD_HAS_SCENE_TIME
void epd_display_time_with_date(
    struct date_time _time, uint16_t battery_mv, int16_t temperature,
    bool full_refresh
);
#endif
#if EPD_HAS_SCENE_LIST
void epd_display_list(
    const char *text, int16_t temperature,
    struct date_time _time, bool full_refresh
);
#endif
