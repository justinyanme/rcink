#ifndef TLSR_LAYOUT_DSL_PARSER_H
#define TLSR_LAYOUT_DSL_PARSER_H

#include <stddef.h>

#include "arena.h"
#include "schema.h"

typedef enum {
    PARSE_OK = 0,
    PARSE_ERR_JSON,
    PARSE_ERR_TYPE,
    PARSE_ERR_ENUM,
    PARSE_ERR_OOM,
    PARSE_ERR_TRUNCATED,
} parse_status_t;

typedef struct {
    parse_status_t status;
    int offset;
    const char *message;
} parse_result_t;

layout_node_t *parse_layout(
    const char *json,
    size_t json_len,
    arena_t *arena,
    parse_result_t *out_result
);

#endif
