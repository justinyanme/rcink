#ifndef TLSR_LAYOUT_ARENA_H
#define TLSR_LAYOUT_ARENA_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t used;
} arena_t;

void arena_init(arena_t *a, uint8_t *buf, size_t cap);
void *arena_alloc(arena_t *a, size_t bytes, size_t align);
void arena_reset(arena_t *a);

#endif
