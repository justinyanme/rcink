#include "pipeline.h"

#include "../dsl/arena.h"
#include "../dsl/parser.h"
#include "../engine/engine.h"
#include "../engine/measure.h"
#include "../proto/uzlib/uzlib.h"
#include "fonts.h"
#include "paint.h"

#include <string.h>

#define PIPE_VIEWPORT_W 250
#define PIPE_VIEWPORT_H 128

static size_t16_t device_text(const measurer_t *m, const text_node_t *t)
{
    (void)m;
    return (size_t16_t){
        (int16_t)(fonts_default.measure_text(t->size, t->weight, t->value, t->value_len) + 2 * t->pad),
        (int16_t)(fonts_default.line_height(t->size) + 2 * t->pad),
    };
}

static size_t16_t device_icon(const measurer_t *m, const icon_node_t *i)
{
    (void)m;
    const int16_t s = i->size == 0 ? 16 : i->size;
    return (size_t16_t){s, s};
}

static size_t16_t device_image(const measurer_t *m, const image_node_t *i)
{
    (void)m;
    return (size_t16_t){(int16_t)i->width, (int16_t)i->height};
}

const measurer_t measurer_device = {
    .text = device_text,
    .icon = device_icon,
    .image = device_image,
    .user = 0,
};

pipeline_status_t pipeline_run(
    const uint8_t *compressed,
    uint16_t compressed_len,
    uint16_t uncompressed_limit,
    uint8_t *inflate_buf,
    uint8_t *arena_buf,
    uint16_t *out_uncompressed_len
)
{
    if (compressed == NULL || inflate_buf == NULL || arena_buf == NULL || uncompressed_limit == 0) {
        return PIPE_ERR_INFLATE;
    }

    struct uzlib_uncomp ctx;
    memset(&ctx, 0, sizeof(ctx));
    uzlib_init();
    uzlib_uncompress_init(&ctx, NULL, 0);
    ctx.source = compressed;
    ctx.source_limit = compressed + compressed_len;
    ctx.source_read_cb = NULL;
    ctx.dest_start = inflate_buf;
    ctx.dest = inflate_buf;
    ctx.dest_limit = inflate_buf + uncompressed_limit;
    if (uzlib_zlib_parse_header(&ctx) < 0 || uzlib_uncompress_chksum(&ctx) != TINF_DONE) {
        return PIPE_ERR_INFLATE;
    }

    const uint16_t uncompressed_len = (uint16_t)(ctx.dest - ctx.dest_start);
    if (out_uncompressed_len != NULL) {
        *out_uncompressed_len = uncompressed_len;
    }

    arena_t arena;
    arena_init(&arena, arena_buf, 4096);
    parse_result_t pr;
    layout_node_t *root = parse_layout((const char *)inflate_buf, uncompressed_len, &arena, &pr);
    if (root == NULL || pr.status != PARSE_OK) {
        return PIPE_ERR_PARSE;
    }

    laid_out_node_t *laid = run_layout(root, (rect_t){0, 0, PIPE_VIEWPORT_W, PIPE_VIEWPORT_H}, &measurer_device, &arena);
    if (laid == NULL) {
        return PIPE_ERR_RENDER;
    }

    paint_layout(laid);
    return PIPE_OK;
}
