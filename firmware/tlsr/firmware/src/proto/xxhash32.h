#ifndef TLSR_LAYOUT_XXHASH32_H
#define TLSR_LAYOUT_XXHASH32_H

#include <stddef.h>
#include <stdint.h>

uint32_t xxhash32(const uint8_t *data, size_t len, uint32_t seed);

#endif
