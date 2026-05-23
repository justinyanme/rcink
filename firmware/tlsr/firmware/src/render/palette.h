#ifndef TLSR_LAYOUT_RENDER_PALETTE_H
#define TLSR_LAYOUT_RENDER_PALETTE_H

#include <stddef.h>
#include <stdint.h>

#include "../dsl/schema.h"

palette_t palette_from_token(const char *s, size_t n);
uint8_t palette_pixel(palette_t p);

#endif
