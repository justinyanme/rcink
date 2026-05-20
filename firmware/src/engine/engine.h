#ifndef TLSR_LAYOUT_ENGINE_H
#define TLSR_LAYOUT_ENGINE_H

#include <stdint.h>

#include "../dsl/arena.h"
#include "../dsl/schema.h"

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

typedef struct {
    int16_t w;
    int16_t h;
} size_t16_t;

struct measurer;

typedef size_t16_t (*measure_text_fn)(const struct measurer *, const text_node_t *);
typedef size_t16_t (*measure_icon_fn)(const struct measurer *, const icon_node_t *);
typedef size_t16_t (*measure_image_fn)(const struct measurer *, const image_node_t *);

typedef struct measurer {
    measure_text_fn text;
    measure_icon_fn icon;
    measure_image_fn image;
    void *user;
} measurer_t;

typedef struct laid_out_node {
    const layout_node_t *node;
    rect_t rect;
    uint8_t child_count;
    struct laid_out_node *children;
} laid_out_node_t;

laid_out_node_t *run_layout(
    const layout_node_t *root,
    rect_t viewport,
    const measurer_t *m,
    arena_t *arena
);

#endif
