#ifndef TLSR_LAYOUT_CRC32_H
#define TLSR_LAYOUT_CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t crc32_zlib(const uint8_t *data, size_t len);
uint32_t crc32_zlib_update(uint32_t state, const uint8_t *data, size_t len);
uint32_t crc32_zlib_finalize(uint32_t state);

#endif
