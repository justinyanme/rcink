#pragma once
#include "types.h"

u16 binary_rle_decode(const u8 *data, u16 data_len, u8 *decoded, u16 decoded_capacity);
u64 lz4dec(const void *src, void *dst, u64 srcsz, u64 dstsz);
