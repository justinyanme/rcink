#include "canvas.h"

#ifndef TLSR_LAYOUT_HOST_BUILD
#include "OneBitDisplay.h"
#include "epd/epd.h"

extern OBDISP obd;
extern unsigned char epd_buffer[EPD_BUFFER_SIZE];
extern unsigned char epd_buffer_red[EPD_BUFFER_SIZE];

static OBDISP red_obd;
static uint8_t ready;

static void ensure_canvas(void)
{
    obdCreateVirtualDisplay(&obd, EPD_WIDTH, EPD_HEIGHT, epd_buffer);
    if (!ready) {
        obdCreateVirtualDisplay(&red_obd, EPD_WIDTH, EPD_HEIGHT, epd_buffer_red);
        ready = 1;
    }
}

void canvas_set_pixel(int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || y < 0 || x >= EPD_WIDTH || y >= EPD_HEIGHT) {
        return;
    }
    ensure_canvas();
    if (color == 2) {
        obdSetPixel(&obd, x, y, 0, 0);
        obdSetPixel(&red_obd, x, y, 1, 0);
    } else if (color == 0) {
        obdSetPixel(&red_obd, x, y, 0, 0);
        obdSetPixel(&obd, x, y, 1, 0);
    } else {
        obdSetPixel(&red_obd, x, y, 0, 0);
        obdSetPixel(&obd, x, y, 0, 0);
    }
}

void canvas_draw_hline(int16_t x, int16_t y, int16_t w, uint8_t color)
{
    for (int16_t ix = 0; ix < w; ix++) {
        canvas_set_pixel((int16_t)(x + ix), y, color);
    }
}

void canvas_draw_vline(int16_t x, int16_t y, int16_t h, uint8_t color)
{
    for (int16_t iy = 0; iy < h; iy++) {
        canvas_set_pixel(x, (int16_t)(y + iy), color);
    }
}

void canvas_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    for (int16_t iy = 0; iy < h; iy++) {
        canvas_draw_hline(x, (int16_t)(y + iy), w, color);
    }
}

void canvas_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color)
{
    int16_t dx = x1 > x0 ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t dy = y1 > y0 ? (int16_t)(y0 - y1) : (int16_t)(y1 - y0);
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = (int16_t)(dx + dy);
    for (;;) {
        canvas_set_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int16_t e2 = (int16_t)(2 * err);
        if (e2 >= dy) {
            err = (int16_t)(err + dy);
            x0 = (int16_t)(x0 + sx);
        }
        if (e2 <= dx) {
            err = (int16_t)(err + dx);
            y0 = (int16_t)(y0 + sy);
        }
    }
}
#endif
