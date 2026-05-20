#ifndef TLSR_LAYOUT_RENDER_ICONS_H
#define TLSR_LAYOUT_RENDER_ICONS_H

#include <stdint.h>

typedef struct {
    const char *name;
    uint8_t w;
    uint8_t h;
    const uint8_t *bits;
} icon_def_t;

const icon_def_t *icon_lookup(const char *name, uint8_t name_len);

#endif
