#ifndef TLSR_LAYOUT_RENDER_FONTS_H
#define TLSR_LAYOUT_RENDER_FONTS_H

#include <stdint.h>

#include "../dsl/schema.h"

typedef struct {
    int16_t (*measure_text)(text_size_t size, text_weight_t weight, const char *utf8, uint8_t len);
    void (*draw_text)(
        int16_t x,
        int16_t y,
        text_size_t size,
        text_weight_t weight,
        const char *utf8,
        uint8_t len,
        uint8_t fg,
        uint8_t bg
    );
    int16_t (*line_height)(text_size_t size);
} font_backend_t;

extern const font_backend_t fonts_default;

#endif
