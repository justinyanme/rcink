#include "primitives.h"

#include "canvas.h"
#include "fonts.h"
#include "icons.h"
#include "palette.h"

#include <string.h>

static int16_t clamp_pct(int16_t pct)
{
    if (pct < 0) {
        return 0;
    }
    if (pct > 100) {
        return 100;
    }
    return pct;
}

static uint8_t color_or_black(palette_t p)
{
    return palette_pixel(p == PAL_UNSET ? PAL_BLACK : p);
}

void draw_divider(const laid_out_node_t *n)
{
    const divider_node_t *d = &n->node->data.divider;
    const int16_t thickness = d->thickness == 0 ? 1 : d->thickness;
    canvas_fill_rect(n->rect.x, n->rect.y, n->rect.w, thickness, color_or_black(d->color));
}

void draw_text(const laid_out_node_t *n)
{
    const text_node_t *t = &n->node->data.text;
    const uint8_t fg = color_or_black(t->color);
    const uint8_t bg = palette_pixel(t->bg);
    if (t->bg != PAL_UNSET) {
        canvas_fill_rect(n->rect.x, n->rect.y, n->rect.w, n->rect.h, bg);
    }

    const int16_t text_w = fonts_default.measure_text(t->size, t->weight, t->value, t->value_len);
    int16_t x = (int16_t)(n->rect.x + t->pad);
    if (t->align == AL_CENTER) {
        x = (int16_t)(n->rect.x + (n->rect.w - text_w) / 2);
    } else if (t->align == AL_END) {
        x = (int16_t)(n->rect.x + n->rect.w - text_w - t->pad);
    }
    const int16_t y = (int16_t)(n->rect.y + t->pad);
    fonts_default.draw_text(x, y, t->size, t->weight, t->value, t->value_len, fg, bg);
}

void draw_hbar(const laid_out_node_t *n)
{
    const hbar_node_t *b = &n->node->data.hbar;
    const uint8_t fg = color_or_black(b->color);
    canvas_fill_rect(n->rect.x, n->rect.y, n->rect.w, 1, 0);
    canvas_fill_rect(n->rect.x, (int16_t)(n->rect.y + n->rect.h - 1), n->rect.w, 1, 0);
    canvas_fill_rect(n->rect.x, n->rect.y, 1, n->rect.h, 0);
    canvas_fill_rect((int16_t)(n->rect.x + n->rect.w - 1), n->rect.y, 1, n->rect.h, 0);
    const int16_t inner_w = n->rect.w > 4 ? (int16_t)(n->rect.w - 4) : 0;
    const int16_t fill = (int16_t)(((int32_t)inner_w * clamp_pct(b->pct)) / 100);
    if (fill > 0 && n->rect.h > 4) {
        canvas_fill_rect((int16_t)(n->rect.x + 2), (int16_t)(n->rect.y + 2), fill, (int16_t)(n->rect.h - 4), fg);
    }
    if (b->label_left[0] != '\0') {
        fonts_default.draw_text((int16_t)(n->rect.x + 4), (int16_t)(n->rect.y + 2), TS_SM, TW_BOLD,
                                b->label_left, (uint8_t)strlen(b->label_left), 1, 0);
    }
    if (b->label_right[0] != '\0') {
        const uint8_t len = (uint8_t)strlen(b->label_right);
        const int16_t w = fonts_default.measure_text(TS_SM, TW_BOLD, b->label_right, len);
        fonts_default.draw_text((int16_t)(n->rect.x + n->rect.w - w - 4), (int16_t)(n->rect.y + 2), TS_SM, TW_BOLD,
                                b->label_right, len, 0, 1);
    }
}

void draw_marker(const laid_out_node_t *n)
{
    const marker_node_t *m = &n->node->data.marker;
    const int16_t x = (int16_t)(n->rect.x + ((int32_t)n->rect.w * clamp_pct(m->pct)) / 100);
    canvas_draw_vline(x, (int16_t)(n->rect.y - 4), (int16_t)(n->rect.h + 8), color_or_black(m->color));
}

void draw_sparkline(const laid_out_node_t *n)
{
    const sparkline_node_t *s = &n->node->data.sparkline;
    if (s->value_count == 0 || n->rect.w <= 0 || n->rect.h <= 0) {
        return;
    }
    int16_t hi = s->values[0];
    for (uint8_t i = 1; i < s->value_count; i++) {
        if (s->values[i] > hi) {
            hi = s->values[i];
        }
    }
    if (hi <= 0) {
        hi = 1;
    }
    const int16_t gutter = 2;
    const int16_t inner_h = n->rect.h > 2 * gutter ? (int16_t)(n->rect.h - 2 * gutter) : 1;
    const uint8_t fg = color_or_black(s->color);
    if (s->value_count == 1) {
        const int16_t y = (int16_t)(n->rect.y + gutter + inner_h - ((int32_t)s->values[0] * inner_h) / hi);
        canvas_fill_rect((int16_t)(n->rect.x + n->rect.w / 2 - 1), (int16_t)(y - 1), 3, 3, fg);
        return;
    }
    for (uint8_t i = 1; i < s->value_count; i++) {
        const int16_t x0 = (int16_t)(n->rect.x + ((int32_t)(i - 1) * (n->rect.w - 1)) / (s->value_count - 1));
        const int16_t x1 = (int16_t)(n->rect.x + ((int32_t)i * (n->rect.w - 1)) / (s->value_count - 1));
        const int16_t y0 = (int16_t)(n->rect.y + gutter + inner_h - ((int32_t)s->values[i - 1] * inner_h) / hi);
        const int16_t y1 = (int16_t)(n->rect.y + gutter + inner_h - ((int32_t)s->values[i] * inner_h) / hi);
        canvas_draw_line(x0, y0, x1, y1, fg);
        canvas_draw_line(x0, (int16_t)(y0 - 1), x1, (int16_t)(y1 - 1), fg);
        canvas_draw_line(x0, (int16_t)(y0 + 1), x1, (int16_t)(y1 + 1), fg);
    }
    for (int16_t x = n->rect.x; x < n->rect.x + n->rect.w; x = (int16_t)(x + 4)) {
        canvas_set_pixel(x, (int16_t)(n->rect.y + n->rect.h - 1), 0);
    }
}

static void blit_1bpp(int16_t x, int16_t y, const icon_def_t *def, uint8_t fg)
{
    const uint8_t bytes_per_row = (uint8_t)((def->w + 7) / 8);
    for (uint8_t row = 0; row < def->h; row++) {
        const uint8_t *p = def->bits + row * bytes_per_row;
        for (uint8_t col = 0; col < def->w; col++) {
            if ((p[col >> 3] & (uint8_t)(0x80 >> (col & 7))) != 0) {
                canvas_set_pixel((int16_t)(x + col), (int16_t)(y + row), fg);
            }
        }
    }
}

void draw_icon(const laid_out_node_t *n)
{
    const icon_node_t *i = &n->node->data.icon;
    const icon_def_t *def = icon_lookup(i->name, (uint8_t)strlen(i->name));
    const uint8_t fg = color_or_black(i->color);
    if (def == NULL) {
        const int16_t s = i->size == 0 ? 16 : i->size;
        canvas_fill_rect((int16_t)(n->rect.x + (n->rect.w - s) / 2), (int16_t)(n->rect.y + (n->rect.h - s) / 2), s, s, fg);
        return;
    }
    blit_1bpp((int16_t)(n->rect.x + (n->rect.w - def->w) / 2), (int16_t)(n->rect.y + (n->rect.h - def->h) / 2), def, fg);
}
