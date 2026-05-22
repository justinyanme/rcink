#include "fonts.h"

#ifndef TLSR_LAYOUT_HOST_BUILD
#include "OneBitDisplay.h"
#include "assets/asset.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "assets/font_classic.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "epd/epd.h"

#include <string.h>

extern OBDISP obd;
extern unsigned char epd_buffer[EPD_BUFFER_SIZE];
extern unsigned char epd_buffer_red[EPD_BUFFER_SIZE];

static OBDISP red_obd;
static uint8_t red_obd_ready;

static uint8_t needs_cjk(const char *s, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        if (((uint8_t)s[i]) >= 0x80) {
            return 1;
        }
    }
    return 0;
}

static GFXfont *font_for(text_size_t size, text_weight_t weight, const char *s, uint8_t len)
{
    (void)weight;
    if (needs_cjk(s, len)) {
        return (GFXfont *)&Dialog_plain_16_zh;
    }
    switch (size) {
    case TS_H1:
        return (GFXfont *)&Classic16x16;
    case TS_H2:
        return (GFXfont *)&Classic12x16;
    case TS_BODY:
        return (GFXfont *)&Classic8x8;
    case TS_SM:
    default:
        return (GFXfont *)&Classic6x8;
    }
}

static int16_t firmware_line_height(text_size_t size)
{
    switch (size) {
    case TS_H1:
        return 28;
    case TS_H2:
        return 22;
    case TS_BODY:
        return 14;
    case TS_SM:
    default:
        return 10;
    }
}

static uint8_t copy_text(char *dst, size_t cap, const char *utf8, uint8_t len)
{
    size_t n = len;
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, utf8, n);
    dst[n] = '\0';
    return (uint8_t)n;
}

static int16_t firmware_measure_text(text_size_t size, text_weight_t weight, const char *utf8, uint8_t len)
{
    char tmp[LAYOUT_MAX_TEXT_LEN];
    const uint8_t copied_len = copy_text(tmp, sizeof(tmp), utf8, len);
    int width = 0;
    int top = 0;
    int bottom = 0;
    obdGetStringBox(font_for(size, weight, tmp, copied_len), tmp, &width, &top, &bottom);
    return (int16_t)width;
}

static int16_t top_to_baseline(GFXfont *font, char *text, int16_t top_y)
{
    int width = 0;
    int top = 0;
    int bottom = 0;
    obdGetStringBox(font, text, &width, &top, &bottom);
    if (width <= 0 || top > bottom) {
        return top_y;
    }
    return (int16_t)(top_y - top);
}

static void ensure_obd(void)
{
    obdCreateVirtualDisplay(&obd, EPD_WIDTH, EPD_HEIGHT, epd_buffer);
    if (!red_obd_ready) {
        obdCreateVirtualDisplay(&red_obd, EPD_WIDTH, EPD_HEIGHT, epd_buffer_red);
        red_obd_ready = 1;
    }
}

static void firmware_draw_text(
    int16_t x,
    int16_t y,
    text_size_t size,
    text_weight_t weight,
    const char *utf8,
    uint8_t len,
    uint8_t fg,
    uint8_t bg
)
{
    (void)bg;
    char tmp[LAYOUT_MAX_TEXT_LEN];
    const uint8_t copied_len = copy_text(tmp, sizeof(tmp), utf8, len);
    ensure_obd();
    GFXfont *font = font_for(size, weight, tmp, copied_len);
    const int16_t baseline_y = top_to_baseline(font, tmp, y);

    if (fg == 0) {
        obdWriteStringCustom(&red_obd, font, x, baseline_y, tmp, 0);
        obdWriteStringCustom(&obd, font, x, baseline_y, tmp, 1);
    } else if (fg == 2) {
        obdWriteStringCustom(&obd, font, x, baseline_y, tmp, 0);
        obdWriteStringCustom(&red_obd, font, x, baseline_y, tmp, 1);
    } else {
        obdWriteStringCustom(&obd, font, x, baseline_y, tmp, 0);
        obdWriteStringCustom(&red_obd, font, x, baseline_y, tmp, 0);
    }
}

const font_backend_t fonts_default = {
    .measure_text = firmware_measure_text,
    .draw_text = firmware_draw_text,
    .line_height = firmware_line_height,
};
#endif
