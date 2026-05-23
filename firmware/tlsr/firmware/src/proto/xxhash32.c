#include "xxhash32.h"

#define XXH_PRIME32_1 0x9E3779B1u
#define XXH_PRIME32_2 0x85EBCA77u
#define XXH_PRIME32_3 0xC2B2AE3Du
#define XXH_PRIME32_4 0x27D4EB2Fu
#define XXH_PRIME32_5 0x165667B1u

static uint32_t rotl32(uint32_t x, uint8_t r)
{
    return (x << r) | (x >> (32 - r));
}

static uint32_t read32le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t round32(uint32_t acc, uint32_t input)
{
    acc += input * XXH_PRIME32_2;
    acc = rotl32(acc, 13);
    acc *= XXH_PRIME32_1;
    return acc;
}

uint32_t xxhash32(const uint8_t *data, size_t len, uint32_t seed)
{
    const uint8_t *p = data;
    const uint8_t *end = p + len;
    uint32_t h32;

    if (len >= 16) {
        const uint8_t *limit = end - 16;
        uint32_t v1 = seed + XXH_PRIME32_1 + XXH_PRIME32_2;
        uint32_t v2 = seed + XXH_PRIME32_2;
        uint32_t v3 = seed + 0;
        uint32_t v4 = seed - XXH_PRIME32_1;

        do {
            v1 = round32(v1, read32le(p));
            p += 4;
            v2 = round32(v2, read32le(p));
            p += 4;
            v3 = round32(v3, read32le(p));
            p += 4;
            v4 = round32(v4, read32le(p));
            p += 4;
        } while (p <= limit);

        h32 = rotl32(v1, 1) + rotl32(v2, 7) + rotl32(v3, 12) + rotl32(v4, 18);
    } else {
        h32 = seed + XXH_PRIME32_5;
    }

    h32 += (uint32_t)len;

    while (p + 4 <= end) {
        h32 += read32le(p) * XXH_PRIME32_3;
        h32 = rotl32(h32, 17) * XXH_PRIME32_4;
        p += 4;
    }

    while (p < end) {
        h32 += (*p) * XXH_PRIME32_5;
        h32 = rotl32(h32, 11) * XXH_PRIME32_1;
        p++;
    }

    h32 ^= h32 >> 15;
    h32 *= XXH_PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= XXH_PRIME32_3;
    h32 ^= h32 >> 16;
    return h32;
}
