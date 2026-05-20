#include "../src/render/fonts.h"
#include "../src/render/canvas.h"

static int16_t host_char_w(text_size_t size)
{
    switch (size) {
    case TS_H1: return 14;
    case TS_H2: return 11;
    case TS_BODY: return 7;
    case TS_SM: return 5;
    }
    return 7;
}

static int16_t host_line_height(text_size_t size)
{
    switch (size) {
    case TS_H1: return 28;
    case TS_H2: return 22;
    case TS_BODY: return 14;
    case TS_SM: return 10;
    }
    return 14;
}

static int16_t host_measure_text(text_size_t size, text_weight_t weight, const char *utf8, uint8_t len)
{
    (void)weight;
    return (int16_t)(len * host_char_w(size));
}

static void draw_host_glyph(int16_t x, int16_t y, int16_t scale_x, int16_t scale_y, char ch, uint8_t fg)
{
    if (ch == ' ') {
        return;
    }
    for (int16_t row = 0; row < 7; row++) {
        for (int16_t col = 0; col < 5; col++) {
            const uint8_t edge = row == 0 || row == 6 || col == 0 || col == 4;
            const uint8_t diag = ((uint8_t)ch + row + col) % 5 == 0;
            if (edge || diag) {
                for (int16_t yy = 0; yy < scale_y; yy++) {
                    for (int16_t xx = 0; xx < scale_x; xx++) {
                        canvas_set_pixel((int16_t)(x + col * scale_x + xx), (int16_t)(y + row * scale_y + yy), fg);
                    }
                }
            }
        }
    }
}

static void host_draw_text(
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
    (void)weight;
    (void)bg;
    const int16_t scale = size == TS_H1 ? 2 : 1;
    const int16_t advance = host_char_w(size);
    for (uint8_t i = 0; i < len; i++) {
        draw_host_glyph((int16_t)(x + i * advance), y, scale, scale, utf8[i], fg);
    }
}

const font_backend_t fonts_default = {
    .measure_text = host_measure_text,
    .draw_text = host_draw_text,
    .line_height = host_line_height,
};
