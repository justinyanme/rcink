#pragma once
#include "etime.h"

#ifndef EPD_ROW_BYTES
#define EPD_ROW_BYTES(width) (((width) + 7) / 8)
#endif

#define EPD_REGION_BYTE_SIZE(width, height) (EPD_ROW_BYTES(width) * (height))

// The Blozi 2.13" BWR panel is the only supported EPD. Past builds had a
// dead EPD_BWR_213 backend; if you re-introduce a panel, gate its header
// with a new EPD_xxx define and add a board pinout under src/epd/.
#ifdef EPD_213
#include "epd_213.h"
#endif

#define EPD_FRAME_BYTES EPD_REGION_BYTE_SIZE(EPD_WIDTH, EPD_HEIGHT)

#if EPD_BUFFER_SIZE < EPD_FRAME_BYTES
#error "EPD_BUFFER_SIZE must be >= the display's row-bytes * height"
#endif

#define MAX_TEXT_SIZE 200

typedef enum
{
    EPD_SCENE_TIME = 0, // default
    EPD_SCENE_LIST,     // display text list, like reminders
    EPD_SCENE_IMAGE,    // display image
    EPD_SCENE_QUOTE,    // display random quote

    EPD_SCENE_DEFAULT = EPD_SCENE_QUOTE,
} EPD_SCENE;

typedef enum {
    EPD_COLOR_WHITE = 0,
    EPD_COLOR_BLACK = 1,
    EPD_COLOR_RED = 2,
} EPD_COLOR;


typedef enum {
    EPD_RM_NONE = 0,
    EPD_RM_FULL = 1,
    EPD_RM_PARTIAL = 2,
    // NOTE: maybe more refresh modes, e.g. custom lut
} EPD_REFRESH_MODE;

void set_force_refresh_when_available(EPD_REFRESH_MODE mode);
void change_scene(EPD_SCENE scene);
// trigger check display state and returns if its updating
bool epd_check_is_updating(void);
void epd_clear_buffer(void);
void epd_fill_ram(EPD_COLOR color);

// returns if update adv data
bool epd_try_update();


void EPD_WriteRam(const uint8_t *image, int width, int height, int left, int top, uint8_t is_red);
void EPD_Trigger_Display(EPD_REFRESH_MODE refresh_mode);
