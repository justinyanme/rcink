#include "arena.h"

#include <string.h>

void arena_init(arena_t *a, uint8_t *buf, size_t cap)
{
    a->buf = buf;
    a->cap = cap;
    a->used = 0;
}

void *arena_alloc(arena_t *a, size_t bytes, size_t align)
{
    if (align == 0) {
        align = 1;
    }
    const size_t aligned = (a->used + (align - 1)) & ~(align - 1);
    if (aligned + bytes > a->cap) {
        return NULL;
    }
    void *p = a->buf + aligned;
    a->used = aligned + bytes;
    memset(p, 0, bytes);
    return p;
}

void arena_reset(arena_t *a)
{
    a->used = 0;
}
