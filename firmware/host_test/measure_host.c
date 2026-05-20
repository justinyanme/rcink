#include "../src/engine/engine.h"
#include "../src/engine/measure.h"

static int16_t width_per_char(text_size_t size)
{
    switch (size) {
    case TS_H1: return 14;
    case TS_H2: return 11;
    case TS_BODY: return 7;
    case TS_SM: return 5;
    }
    return 7;
}

static size_t16_t host_text(const measurer_t *m, const text_node_t *t)
{
    return (size_t16_t){
        (int16_t)(t->value_len * width_per_char(t->size) + 2 * t->pad),
        (int16_t)(layout_font_height(t->size) + 2 * t->pad),
    };
}

static size_t16_t host_icon(const measurer_t *m, const icon_node_t *i)
{
    const int16_t s = i->size == 0 ? 16 : i->size;
    return (size_t16_t){s, s};
}

static size_t16_t host_image(const measurer_t *m, const image_node_t *i)
{
    return (size_t16_t){(int16_t)i->width, (int16_t)i->height};
}

const measurer_t host_measurer = {
    .text = host_text,
    .icon = host_icon,
    .image = host_image,
    .user = 0,
};
