#include "crc32.h"

uint32_t crc32_zlib_update(uint32_t state, const uint8_t *data, size_t len)
{
    uint32_t c = state ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(c & 1u);
            c = (c >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return c ^ 0xFFFFFFFFu;
}

uint32_t crc32_zlib_finalize(uint32_t state)
{
    return state;
}

uint32_t crc32_zlib(const uint8_t *data, size_t len)
{
    return crc32_zlib_update(0, data, len);
}
