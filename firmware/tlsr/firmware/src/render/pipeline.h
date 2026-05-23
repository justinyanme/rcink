#ifndef TLSR_LAYOUT_RENDER_PIPELINE_H
#define TLSR_LAYOUT_RENDER_PIPELINE_H

#include <stdint.h>

typedef enum {
    PIPE_OK = 0,
    PIPE_ERR_INFLATE,
    PIPE_ERR_PARSE,
    PIPE_ERR_RENDER,
} pipeline_status_t;

pipeline_status_t pipeline_run(
    const uint8_t *compressed,
    uint16_t compressed_len,
    uint16_t uncompressed_limit,
    uint8_t *inflate_buf,
    uint8_t *arena_buf,
    uint16_t *out_uncompressed_len
);

#endif
