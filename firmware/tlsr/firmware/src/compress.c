#include "compress.h"

#include <stdint.h>

#define BINARY_BYTE_COUNT 6

static const u8 binary_bytes[BINARY_BYTE_COUNT] = {0x00, 0xff, 0xc0, 0x7f, 0xfc, 0xfe};

static bool is_binary_byte(const u8 b)
{
    for (u8 i = 0; i < BINARY_BYTE_COUNT; i++) {
        if (b == binary_bytes[i]) {
            return true;
        }
    }
    return false;
}

u16 binary_rle_decode(const u8 *data, const u16 data_len, u8 *decoded, const u16 decoded_capacity)
{
    if (data == NULL || decoded == NULL) {
        return 0;
    }

    u16 i = 0, j = 0;

    while (i < data_len) {
        const u8 current_byte = data[i];
        u16 run_len = 1;
        if (i + 1 < data_len && is_binary_byte(current_byte)) {
            run_len = data[i + 1];
            i++;
        }
        if (run_len > decoded_capacity - j) {
            return 0;
        }
        for (u16 k = 0; k < run_len; k++) {
            decoded[j++] = current_byte;
        }
        i++;
    }

    return j;
}

static bool add_overflow_uintptr(const uintptr_t a, const u64 b, uintptr_t *out)
{
    if (out == NULL || b > UINTPTR_MAX || a > UINTPTR_MAX - (uintptr_t)b) {
        return true;
    }
    *out = a + (uintptr_t)b;
    return false;
}

static bool add_overflow_u64(const u64 a, const u64 b, u64 *out)
{
    if (out == NULL || a > UINT64_MAX - b) {
        return true;
    }
    *out = a + b;
    return false;
}

static bool sub_overflow_uintptr(const uintptr_t a, const uintptr_t b, uintptr_t *out)
{
    if (out == NULL || b > a) {
        return true;
    }
    *out = a - b;
    return false;
}

u64 lz4dec(const void *src, void *dst, u64 srcsz, u64 dstsz)
{
    if (src == NULL || dst == NULL) {
        return 0;
    }

    const uint8_t *x0 = src;
    uint8_t *x1 = dst;
    uintptr_t x2p;
    if (add_overflow_uintptr((uintptr_t)x0, srcsz, &x2p)) return 0;
    const uint8_t *x2 = (const uint8_t*)x2p;
    uintptr_t x3p;
    if (add_overflow_uintptr((uintptr_t)x1, dstsz, &x3p)) return 0;
    uint8_t *x3 = (uint8_t*)x3p;
    while (1) {
        uint8_t w6;
        if (x0 >= x2) return 0;
        uint8_t token = *x0++;
        u64 litlen = token >> 4;
        u64 matchlen = token & 0xf;
        if (litlen == 0xf) {
            do {
                if (x0 >= x2) return 0;
                w6 = *x0++;
                if (add_overflow_u64(litlen, w6, &litlen)) return 0;
            } while (w6 == 0xff);
        }
        if (!((x1 < x0 || x1 >= x2) && x0 < x2 && x1 < x3 && litlen <= (x2 - x0) && litlen <= (x3 - x1))) return 0;
        for (; litlen > 0; --litlen) {
            *x1++ = *x0++;
        }
        if (matchlen == 0 && x0 >= x2) return x1 - (uint8_t*)dst;
        if (x2 - x0 < 2) return 0;
        uint16_t offset = (uint16_t)x0[0] | ((uint16_t)x0[1] << 8);
        x0 += 2;
        if (!offset) return 0;
        if (matchlen == 0xf) {
            do {
                if (x0 >= x2) return 0;
                w6 = *x0++;
                if (add_overflow_u64(matchlen, w6, &matchlen)) return 0;
            } while (w6 == 0xff);
        }
        if (add_overflow_u64(matchlen, 4, &matchlen)) return 0;
        uintptr_t x5p;
        if (sub_overflow_uintptr((uintptr_t)x1, offset, &x5p)) return 0;
        uint8_t *x5 = (uint8_t*)x5p;
        if (x5 < (uint8_t*)dst || matchlen > (x3 - x1)) return 0;
        for (; matchlen > 0; --matchlen) {
            *x1++ = *x5++;
        }
    }
}
