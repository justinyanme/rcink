#include "primitives.h"

#include "canvas.h"
#include "fonts.h"

void draw_image(const laid_out_node_t *n)
{
    const int16_t x0 = n->rect.x;
    const int16_t y0 = n->rect.y;
    const int16_t w = n->rect.w;
    const int16_t h = n->rect.h;
    canvas_fill_rect(x0, y0, w, 1, 0);
    canvas_fill_rect(x0, (int16_t)(y0 + h - 1), w, 1, 0);
    canvas_fill_rect(x0, y0, 1, h, 0);
    canvas_fill_rect((int16_t)(x0 + w - 1), y0, 1, h, 0);
    for (int16_t x = (int16_t)(x0 - h); x < x0 + w; x = (int16_t)(x + 8)) {
        canvas_draw_line(x, (int16_t)(y0 + h - 1), (int16_t)(x + h - 1), y0, 0);
    }
    fonts_default.draw_text((int16_t)(x0 + (w - 18) / 2), (int16_t)(y0 + (h - 10) / 2), TS_SM, TW_BOLD, "IMG", 3, 0, 1);
}
