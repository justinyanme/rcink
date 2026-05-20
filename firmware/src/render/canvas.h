#ifndef TLSR_LAYOUT_RENDER_CANVAS_H
#define TLSR_LAYOUT_RENDER_CANVAS_H

#include <stdint.h>

void canvas_set_pixel(int16_t x, int16_t y, uint8_t color);
void canvas_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void canvas_draw_hline(int16_t x, int16_t y, int16_t w, uint8_t color);
void canvas_draw_vline(int16_t x, int16_t y, int16_t h, uint8_t color);
void canvas_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);

#endif
