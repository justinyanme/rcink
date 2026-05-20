#pragma once

#include "app_config.h"

#ifdef EPD_213

#define EPD_WIDTH       250
#define EPD_HEIGHT      128  // needs to be multiple of 8
#define EPD_BUFFER_SIZE (300 * 128 / 8)

bool EPD_Display_setup(bool soft_reset);
void EPD_trigger_display_impl(bool full_refresh);
void EPD_WriteRam_impl(const uint8_t *image, int width, int height, int left, int top, bool is_red);
void EPD_Load_LUT();

u8 EPD_read_temp_impl(void);
u8 EPD_Display_BWR_impl(unsigned char *image, unsigned char *red_image, u8 full_refresh);
void EPD_set_sleep_impl(void);

#endif
