#ifndef TLSR_LAYOUT_ENGINE_MEASURE_H
#define TLSR_LAYOUT_ENGINE_MEASURE_H

#include <stdbool.h>

#include "engine.h"

#define LAYOUT_UNBOUNDED 0x4000

int16_t layout_font_height(text_size_t size);
size_t16_t measure_natural(
    const layout_node_t *node,
    const measurer_t *m,
    int16_t parent_cross,
    bool vertical
);

#endif
