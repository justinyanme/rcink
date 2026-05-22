#include "../src/engine/engine.h"
#include "../src/engine/measure.h"
#include "../src/render/fonts.h"

static size_t16_t host_text(const measurer_t *m, const text_node_t *t)
{
    return (size_t16_t){
        (int16_t)(fonts_default.measure_text(t->size, t->weight, t->value, t->value_len) + 2 * t->pad),
        (int16_t)(fonts_default.line_height(t->size) + 2 * t->pad),
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
