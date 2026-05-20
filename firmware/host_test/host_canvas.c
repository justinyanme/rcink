#include "host_canvas.h"

#include "../src/render/canvas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define HOST_CANVAS_MAX_W 250
#define HOST_CANVAS_MAX_H 128

static uint8_t pixels[HOST_CANVAS_MAX_W * HOST_CANVAS_MAX_H];
static int16_t canvas_w;
static int16_t canvas_h;

void host_canvas_init(int16_t w, int16_t h)
{
    canvas_w = w <= HOST_CANVAS_MAX_W ? w : HOST_CANVAS_MAX_W;
    canvas_h = h <= HOST_CANVAS_MAX_H ? h : HOST_CANVAS_MAX_H;
    host_canvas_clear(1);
}

void host_canvas_clear(uint8_t color)
{
    memset(pixels, color, sizeof(pixels));
}

void *host_canvas_buffer(int16_t *w, int16_t *h)
{
    if (w != NULL) {
        *w = canvas_w;
    }
    if (h != NULL) {
        *h = canvas_h;
    }
    return pixels;
}

void canvas_set_pixel(int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || y < 0 || x >= canvas_w || y >= canvas_h) {
        return;
    }
    pixels[y * canvas_w + x] = color > 2 ? 1 : color;
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
        const int16_t e2 = (int16_t)(2 * err);
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

static void rgb_for(uint8_t p, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (p == 0) {
        *r = 0x00;
        *g = 0x00;
        *b = 0x00;
    } else if (p == 2) {
        *r = 0xD9;
        *g = 0x20;
        *b = 0x27;
    } else {
        *r = 0xFF;
        *g = 0xFF;
        *b = 0xFF;
    }
}

int host_canvas_write_png(const char *path)
{
    uint8_t rgb[HOST_CANVAS_MAX_W * HOST_CANVAS_MAX_H * 3];
    for (int32_t i = 0; i < (int32_t)canvas_w * canvas_h; i++) {
        rgb_for(pixels[i], &rgb[i * 3], &rgb[i * 3 + 1], &rgb[i * 3 + 2]);
    }
    return stbi_write_png(path, canvas_w, canvas_h, 3, rgb, canvas_w * 3) != 0 ? 0 : -1;
}

int host_canvas_write_pgm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    fprintf(f, "P5\n%d %d\n255\n", canvas_w, canvas_h);
    for (int32_t i = 0; i < (int32_t)canvas_w * canvas_h; i++) {
        const uint8_t v = pixels[i] == 0 ? 0x00 : pixels[i] == 2 ? 0x70 : 0xFF;
        fwrite(&v, 1, 1, f);
    }
    fclose(f);
    return 0;
}
