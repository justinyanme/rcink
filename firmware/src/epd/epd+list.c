#include <log.h>
#include <string.h>

#include "etime.h"
#include "tl_common.h"
#include "epd.h"
#include "epd_private.h"

#include "app_config.h"

#include "vendor_include.h"
#include "OneBitDisplay.h"

#include "assets/asset.h"

extern u8 epd_buffer[EPD_BUFFER_SIZE];
extern u8 epd_buffer_red[EPD_BUFFER_SIZE];
extern OBDISP obd;

enum {
    LIST_MAX_LINES = 6,
    LIST_MAX_LINE_CHARS = 31,
};

typedef enum {
    LINE_COLOR_BLACK = 0,
    LINE_COLOR_RED = 1,
} list_line_color_t;

typedef struct {
    char text[LIST_MAX_LINE_CHARS + 1];
    list_line_color_t colors[LIST_MAX_LINE_CHARS];
    uint8_t stored_len;
    bool has_text;
} list_line_t;

static void list_line_finalize(list_line_t *line, uint16_t logical_len)
{
    if (line == NULL) {
        return;
    }
    line->has_text = (logical_len > 0);
    if (line->stored_len > LIST_MAX_LINE_CHARS) {
        line->stored_len = LIST_MAX_LINE_CHARS;
    }
    line->text[line->stored_len] = '\0';
}

static bool list_try_consume_markup(const char **cursor, list_line_color_t *active_color)
{
    if (cursor == NULL || *cursor == NULL || active_color == NULL) {
        return false;
    }

    const char *p = *cursor;
    if (*p != '<') {
        return false;
    }

    const char *next = p + 1;
    if (*next == '\0') {
        return false;
    }

    if (*next == 'r' || *next == 'R') {
        next++;
        if (*next == '\0') {
            return false;
        }
        if (*next == '>') {
            *active_color = LINE_COLOR_RED;
            *cursor = next + 1;
            return true;
        }
        return false;
    }

    if (*next == '/') {
        next++;
        if (*next == '\0') {
            return false;
        }
        const char color_token = *next;
        if (color_token != 'r' && color_token != 'R') {
            return false;
        }
        next++;
        if (*next == '\0') {
            return false;
        }
        if (*next == '>') {
            *active_color = LINE_COLOR_BLACK;
            *cursor = next + 1;
            return true;
        }
    }

    return false;
}

void epd_display_list(
    const char *text, const int16_t temperature,
    const struct date_time _time, const bool full_refresh
) {
    epd_clear_buffer();

    obdCreateVirtualDisplay(&obd, EPD_WIDTH, EPD_HEIGHT, epd_buffer);
    obdFill(&obd, 0, 0); // fill with white

    OBDISP red_obd = {0};
    obdCreateVirtualDisplay(&red_obd, EPD_WIDTH, EPD_HEIGHT, epd_buffer_red);
    obdFill(&red_obd, 0, 0);

    char buff[32];

    const char *p = (text == NULL || text[0] == 0) ? "\n\n           No text" : text;
    const u8 line_height = 17;
    const u8 y_offset = 18;
    const u8 x_offset = 5;

    list_line_t lines[LIST_MAX_LINES];
    memset(lines, 0, sizeof(lines));
    u8 slot = 0;
    uint16_t logical_len = 0;

    list_line_color_t active_color = LINE_COLOR_BLACK;

    while (*p && slot < LIST_MAX_LINES) {
        if (list_try_consume_markup(&p, &active_color)) {
            continue;
        }
        if (*p == '\r') {
            p++;
            continue;
        }
        if (*p == '\n') {
            list_line_finalize(&lines[slot], logical_len);
            slot++;
            logical_len = 0;
            p++;
            continue;
        }

        if (lines[slot].stored_len < LIST_MAX_LINE_CHARS) {
            lines[slot].text[lines[slot].stored_len++] = *p;
            lines[slot].colors[lines[slot].stored_len - 1] = active_color;
            lines[slot].text[lines[slot].stored_len] = '\0';
        }
        logical_len++;
        p++;
    }

    if (slot < LIST_MAX_LINES && logical_len > 0) {
        list_line_finalize(&lines[slot], logical_len);
        slot++;
    }

    const u8 line_slots = slot;

    for (u8 idx = 0; idx < line_slots; idx++) {
        if (!lines[idx].has_text) {
            continue;
        }

        const int baseline_y = y_offset + idx * line_height;
        int cursor_x = x_offset;
        uint8_t consumed = 0;
        while (consumed < lines[idx].stored_len) {
            const list_line_color_t segment_color = lines[idx].colors[consumed];
            uint8_t segment_len = 1;
            while ((consumed + segment_len) < lines[idx].stored_len &&
                   lines[idx].colors[consumed + segment_len] == segment_color)
            {
                segment_len++;
            }
            char segment[LIST_MAX_LINE_CHARS + 1];
            memcpy(segment, &lines[idx].text[consumed], segment_len);
            segment[segment_len] = '\0';

            int segment_width = 0;
            int dummy_top = 0;
            int dummy_bottom = 0;
            obdGetStringBox((GFXfont*)&Dialog_plain_16, segment, &segment_width, &dummy_top, &dummy_bottom);

            OBDISP *target = (segment_color == LINE_COLOR_RED) ? &red_obd : &obd;
            obdWriteStringCustom(target, (GFXfont*)&Dialog_plain_16, cursor_x, baseline_y, segment, 1);

            cursor_x += segment_width;
            consumed += segment_len;
        }
    }


    // date
    sprintf(buff, "%d.%d", _time.tm_month, _time.tm_day);
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16, 5, 120, buff, 1);

    // temperature
    sprintf(buff, "%d°C", temperature);
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16, 65, 120, buff, 1);

    // week
    if (_time.tm_week == 6) {
        // sunday
        sprintf(buff, "9:%c", 0x20 + 13);
    } else {
        // 0x21: 一 [1]
        sprintf(buff, "9:%c", _time.tm_week + 1 + 0x20);
    }
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16_zh, 95, 122, buff, 1);

    // time
    sprintf(buff, "%02d:%02d", _time.tm_hour, _time.tm_min);
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16, 200, 120, buff, 1);

    FixBuffer();
    EPD_Display(epd_buffer, epd_buffer_red, full_refresh);
}
