#include "../src/render/fonts.h"
#include "../src/render/canvas.h"
#include "../src/OneBitDisplay.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "../src/assets/font_classic.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

static const GFXfont *font_for(text_size_t size)
{
    switch (size) {
    case TS_H1:
        return &Classic16x16;
    case TS_H2:
        return &Classic12x16;
    case TS_BODY:
        return &Classic8x8;
    case TS_SM:
    default:
        return &Classic6x8;
    }
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
    const GFXfont *font = font_for(size);
    int16_t width = 0;
    for (uint8_t i = 0; i < len; i++) {
        const unsigned char ch = (unsigned char)utf8[i];
        if (ch < font->first || ch > font->last) {
            continue;
        }
        width = (int16_t)(width + font->glyph[ch - font->first].xAdvance);
    }
    return width;
}

static void host_text_box(
    const GFXfont *font,
    const char *utf8,
    uint8_t len,
    int16_t *width,
    int16_t *top,
    int16_t *bottom
)
{
    int16_t w = 0;
    int16_t min_y = 32767;
    int16_t max_y = -32768;
    for (uint8_t i = 0; i < len; i++) {
        const unsigned char ch = (unsigned char)utf8[i];
        if (ch < font->first || ch > font->last) {
            continue;
        }
        const GFXglyph *glyph = &font->glyph[ch - font->first];
        w = (int16_t)(w + glyph->xAdvance);
        if (glyph->yOffset < min_y) {
            min_y = glyph->yOffset;
        }
        const int16_t glyph_bottom = (int16_t)(glyph->height + glyph->yOffset);
        if (glyph_bottom > max_y) {
            max_y = glyph_bottom;
        }
    }
    *width = w;
    *top = min_y == 32767 ? 0 : min_y;
    *bottom = max_y == -32768 ? 0 : max_y;
}

static void draw_host_glyph(const GFXfont *font, int16_t x, int16_t baseline_y, char ch, uint8_t fg)
{
    const unsigned char codepoint = (unsigned char)ch;
    if (codepoint < font->first || codepoint > font->last) {
        return;
    }

    const GFXglyph *glyph = &font->glyph[codepoint - font->first];
    uint32_t bit = 0;
    for (uint8_t row = 0; row < glyph->height; row++) {
        for (uint8_t col = 0; col < glyph->width; col++) {
            const uint8_t b = font->bitmap[glyph->bitmapOffset + (bit >> 3)];
            const uint8_t mask = (uint8_t)(0x80u >> (bit & 7u));
            if ((b & mask) != 0) {
                canvas_set_pixel(
                    (int16_t)(x + glyph->xOffset + col),
                    (int16_t)(baseline_y + glyph->yOffset + row),
                    fg
                );
            }
            bit++;
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
    const GFXfont *font = font_for(size);
    int16_t width = 0;
    int16_t top = 0;
    int16_t bottom = 0;
    host_text_box(font, utf8, len, &width, &top, &bottom);
    (void)width;
    (void)bottom;
    const int16_t baseline_y = (int16_t)(y - top);

    int16_t cursor_x = x;
    for (uint8_t i = 0; i < len; i++) {
        const unsigned char ch = (unsigned char)utf8[i];
        if (ch < font->first || ch > font->last) {
            continue;
        }
        draw_host_glyph(font, cursor_x, baseline_y, utf8[i], fg);
        cursor_x = (int16_t)(cursor_x + font->glyph[ch - font->first].xAdvance);
    }
}

const font_backend_t fonts_default = {
    .measure_text = host_measure_text,
    .draw_text = host_draw_text,
    .line_height = host_line_height,
};
