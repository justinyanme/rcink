#include "fonts.h"

#ifndef TLSR_LAYOUT_HOST_BUILD
#include "OneBitDisplay.h"
#include "assets/asset.h"
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
        switch (size) {
        case TS_H1:
            return (GFXfont *)&WenQuanYi_Bitmap_Song_16px16pt8b;
        case TS_H2:
            return (GFXfont *)&WenQuanYi_Bitmap_Song_16px12pt8b;
        case TS_SM:
            return (GFXfont *)&WenQuanYi_Bitmap_Song_16px9pt8b;
        case TS_BODY:
        default:
            return (GFXfont *)&WenQuanYi_Bitmap_Song_16px10pt8b;
        }
    }
    switch (size) {
    case TS_H1:
        return (GFXfont *)&Classic16x16;
    case TS_H2:
        return (GFXfont *)&Classic12x16;
    case TS_SM:
        return (GFXfont *)&Classic6x8;
    case TS_BODY:
    default:
        return (GFXfont *)&Classic8x8;
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

static void copy_text(char *dst, size_t cap, const char *utf8, uint8_t len)
{
    size_t n = len;
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, utf8, n);
    dst[n] = '\0';
}

static int16_t firmware_measure_text(text_size_t size, text_weight_t weight, const char *utf8, uint8_t len)
{
    char tmp[LAYOUT_MAX_TEXT_LEN];
    copy_text(tmp, sizeof(tmp), utf8, len);
    int width = 0;
    int top = 0;
    int bottom = 0;
    obdGetStringBox(font_for(size, weight, tmp, (uint8_t)strlen(tmp)), tmp, &width, &top, &bottom);
    return (int16_t)width;
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
    copy_text(tmp, sizeof(tmp), utf8, len);
    ensure_obd();
    OBDISP *target = fg == 2 ? &red_obd : &obd;
    if (fg != 1) {
        obdWriteStringCustom(target, font_for(size, weight, tmp, (uint8_t)strlen(tmp)), x, y, tmp, 1);
    }
}

const font_backend_t fonts_default = {
    .measure_text = firmware_measure_text,
    .draw_text = firmware_draw_text,
    .line_height = firmware_line_height,
};
#endif
