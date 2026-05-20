#include "parser.h"

#include "jsmn.h"
#include "../render/palette.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define JSON_MAX_TOKENS 512

typedef struct {
    const char *json;
    size_t json_len;
    const jsmntok_t *tok;
    int tok_count;
    arena_t *arena;
    parse_result_t result;
} parser_t;

static void set_error(parser_t *p, parse_status_t status, int off, const char *msg)
{
    if (p->result.status == PARSE_OK) {
        p->result.status = status;
        p->result.offset = off;
        p->result.message = msg;
    }
}

static bool tok_eq(parser_t *p, int idx, const char *s)
{
    const jsmntok_t *t = &p->tok[idx];
    const size_t n = (size_t)(t->end - t->start);
    return t->type == JSMN_STRING && strlen(s) == n && memcmp(p->json + t->start, s, n) == 0;
}

static int tok_next(parser_t *p, int idx)
{
    if (idx < 0 || idx >= p->tok_count) {
        return p->tok_count;
    }
    const jsmntok_t *t = &p->tok[idx];
    int out = idx + 1;
    if (t->type == JSMN_OBJECT) {
        for (int i = 0; i < t->size * 2 && out < p->tok_count; i++) {
            out = tok_next(p, out);
        }
    } else if (t->type == JSMN_ARRAY) {
        for (int i = 0; i < t->size && out < p->tok_count; i++) {
            out = tok_next(p, out);
        }
    }
    return out;
}

static int obj_find(parser_t *p, int obj, const char *key)
{
    if (obj < 0 || obj >= p->tok_count || p->tok[obj].type != JSMN_OBJECT) {
        return -1;
    }
    int idx = obj + 1;
    for (int i = 0; i < p->tok[obj].size && idx < p->tok_count; i++) {
        const int key_idx = idx;
        const int val_idx = idx + 1;
        if (key_idx < p->tok_count && val_idx < p->tok_count && tok_eq(p, key_idx, key)) {
            return val_idx;
        }
        idx = tok_next(p, val_idx);
    }
    return -1;
}

static bool token_number(parser_t *p, int idx, int32_t *out)
{
    if (idx < 0 || idx >= p->tok_count || p->tok[idx].type != JSMN_PRIMITIVE) {
        return false;
    }
    const jsmntok_t *t = &p->tok[idx];
    const size_t n = (size_t)(t->end - t->start);
    if (n == 0 || n >= 16) {
        return false;
    }
    char tmp[16];
    memcpy(tmp, p->json + t->start, n);
    tmp[n] = '\0';
    char *end = NULL;
    long v = strtol(tmp, &end, 10);
    if (end == tmp || *end != '\0') {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

static int16_t token_i16_default(parser_t *p, int idx, int16_t fallback)
{
    int32_t v = fallback;
    if (idx >= 0 && token_number(p, idx, &v)) {
        if (v < INT16_MIN) {
            return INT16_MIN;
        }
        if (v > INT16_MAX) {
            return INT16_MAX;
        }
    }
    return (int16_t)v;
}

static uint8_t token_u8_default(parser_t *p, int idx, uint8_t fallback)
{
    int32_t v = fallback;
    if (idx >= 0 && token_number(p, idx, &v)) {
        if (v < 0) {
            return 0;
        }
        if (v > UINT8_MAX) {
            return UINT8_MAX;
        }
    }
    return (uint8_t)v;
}

static int16_t pct_default_clamped(parser_t *p, int idx, int16_t fallback)
{
    int16_t pct = token_i16_default(p, idx, fallback);
    if (pct < 0) {
        return 0;
    }
    if (pct > 100) {
        return 100;
    }
    return pct;
}

static uint8_t copy_string(parser_t *p, int idx, char *dst, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    dst[0] = '\0';
    if (idx < 0 || idx >= p->tok_count || p->tok[idx].type != JSMN_STRING) {
        return 0;
    }
    const jsmntok_t *t = &p->tok[idx];
    size_t n = (size_t)(t->end - t->start);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, p->json + t->start, n);
    dst[n] = '\0';
    return (uint8_t)n;
}

static bool parse_color(parser_t *p, int obj, const char *key, palette_t fallback, palette_t *out)
{
    const int idx = obj_find(p, obj, key);
    if (idx < 0) {
        *out = fallback;
        return true;
    }
    if (p->tok[idx].type != JSMN_STRING) {
        set_error(p, PARSE_ERR_TYPE, p->tok[idx].start, "color must be string");
        return false;
    }
    palette_t c = palette_from_token(p->json + p->tok[idx].start, (size_t)(p->tok[idx].end - p->tok[idx].start));
    if (c == PAL_UNSET) {
        set_error(p, PARSE_ERR_ENUM, p->tok[idx].start, "unknown color");
        return false;
    }
    *out = c;
    return true;
}

static text_size_t parse_text_size(parser_t *p, int idx)
{
    if (idx < 0) {
        return TS_BODY;
    }
    if (tok_eq(p, idx, "h1")) {
        return TS_H1;
    }
    if (tok_eq(p, idx, "h2")) {
        return TS_H2;
    }
    if (tok_eq(p, idx, "body")) {
        return TS_BODY;
    }
    if (tok_eq(p, idx, "sm")) {
        return TS_SM;
    }
    set_error(p, PARSE_ERR_ENUM, p->tok[idx].start, "unknown text size");
    return TS_BODY;
}

static text_weight_t parse_weight(parser_t *p, int idx)
{
    if (idx < 0 || tok_eq(p, idx, "regular")) {
        return TW_REGULAR;
    }
    if (tok_eq(p, idx, "bold")) {
        return TW_BOLD;
    }
    set_error(p, PARSE_ERR_ENUM, p->tok[idx].start, "unknown text weight");
    return TW_REGULAR;
}

static align_t parse_align(parser_t *p, int idx)
{
    if (idx < 0 || tok_eq(p, idx, "start") || tok_eq(p, idx, "left")) {
        return AL_START;
    }
    if (tok_eq(p, idx, "center")) {
        return AL_CENTER;
    }
    if (tok_eq(p, idx, "end") || tok_eq(p, idx, "right")) {
        return AL_END;
    }
    set_error(p, PARSE_ERR_ENUM, p->tok[idx].start, "unknown align");
    return AL_START;
}

static bool type_is(parser_t *p, int idx, const char *name)
{
    return idx >= 0 && idx < p->tok_count && tok_eq(p, idx, name);
}

static layout_node_t *parse_node(parser_t *p, int obj);

static void parse_stack(parser_t *p, int obj, layout_node_t *n)
{
    n->data.stack.gap = token_u8_default(p, obj_find(p, obj, "gap"), 0);
    n->data.stack.pad = token_u8_default(p, obj_find(p, obj, "pad"), 0);
    n->data.stack.align = parse_align(p, obj_find(p, obj, "align"));
    const int children = obj_find(p, obj, "children");
    if (children < 0) {
        return;
    }
    if (p->tok[children].type != JSMN_ARRAY) {
        set_error(p, PARSE_ERR_TYPE, p->tok[children].start, "children must be array");
        return;
    }
    int idx = children + 1;
    for (int i = 0; i < p->tok[children].size && idx < p->tok_count; i++) {
        if (n->data.stack.child_count < LAYOUT_MAX_CHILDREN) {
            layout_node_t *child = parse_node(p, idx);
            if (child != NULL) {
                n->data.stack.children[n->data.stack.child_count++] = child;
            }
        }
        idx = tok_next(p, idx);
        if (p->result.status != PARSE_OK) {
            return;
        }
    }
}

static void parse_sparkline(parser_t *p, int obj, layout_node_t *n)
{
    if (!parse_color(p, obj, "color", PAL_BLACK, &n->data.sparkline.color)) {
        return;
    }
    n->data.sparkline.height = token_u8_default(p, obj_find(p, obj, "height"), 24);
    const int values = obj_find(p, obj, "values");
    if (values < 0) {
        return;
    }
    if (p->tok[values].type != JSMN_ARRAY) {
        set_error(p, PARSE_ERR_TYPE, p->tok[values].start, "values must be array");
        return;
    }
    int idx = values + 1;
    for (int i = 0; i < p->tok[values].size && idx < p->tok_count; i++) {
        if (n->data.sparkline.value_count < LAYOUT_MAX_SPARKLINE) {
            n->data.sparkline.values[n->data.sparkline.value_count++] = token_i16_default(p, idx, 0);
        }
        idx = tok_next(p, idx);
    }
}

static layout_node_t *parse_node(parser_t *p, int obj)
{
    if (obj < 0 || obj >= p->tok_count || p->tok[obj].type != JSMN_OBJECT) {
        set_error(p, PARSE_ERR_TYPE, obj >= 0 && obj < p->tok_count ? p->tok[obj].start : 0, "node must be object");
        return NULL;
    }
    const int type_idx = obj_find(p, obj, "type");
    if (type_idx < 0 || p->tok[type_idx].type != JSMN_STRING) {
        set_error(p, PARSE_ERR_TYPE, p->tok[obj].start, "node type required");
        return NULL;
    }

    layout_node_t *n = arena_alloc(p->arena, sizeof(*n), sizeof(void *));
    if (n == NULL) {
        set_error(p, PARSE_ERR_OOM, p->tok[obj].start, "arena exhausted");
        return NULL;
    }

    if (type_is(p, type_idx, "vstack")) {
        n->kind = LN_VSTACK;
        parse_stack(p, obj, n);
    } else if (type_is(p, type_idx, "hstack")) {
        n->kind = LN_HSTACK;
        parse_stack(p, obj, n);
    } else if (type_is(p, type_idx, "spacer")) {
        n->kind = LN_SPACER;
        n->data.spacer.size = token_i16_default(p, obj_find(p, obj, "size"), -1);
    } else if (type_is(p, type_idx, "divider")) {
        n->kind = LN_DIVIDER;
        if (parse_color(p, obj, "color", PAL_BLACK, &n->data.divider.color)) {
            n->data.divider.thickness = token_u8_default(p, obj_find(p, obj, "thickness"), 1);
            if (n->data.divider.thickness == 0) {
                n->data.divider.thickness = 1;
            }
        }
    } else if (type_is(p, type_idx, "text")) {
        n->kind = LN_TEXT;
        n->data.text.value_len = copy_string(p, obj_find(p, obj, "value"), n->data.text.value, sizeof(n->data.text.value));
        n->data.text.size = parse_text_size(p, obj_find(p, obj, "size"));
        n->data.text.weight = parse_weight(p, obj_find(p, obj, "weight"));
        n->data.text.align = parse_align(p, obj_find(p, obj, "align"));
        n->data.text.pad = token_u8_default(p, obj_find(p, obj, "pad"), 0);
        if (p->result.status == PARSE_OK && parse_color(p, obj, "color", PAL_BLACK, &n->data.text.color)) {
            (void)parse_color(p, obj, "bg", PAL_UNSET, &n->data.text.bg);
        }
    } else if (type_is(p, type_idx, "hbar")) {
        n->kind = LN_HBAR;
        n->data.hbar.pct = pct_default_clamped(p, obj_find(p, obj, "pct"), 0);
        n->data.hbar.height = token_u8_default(p, obj_find(p, obj, "height"), 12);
        copy_string(p, obj_find(p, obj, "label_left"), n->data.hbar.label_left, sizeof(n->data.hbar.label_left));
        copy_string(p, obj_find(p, obj, "label_right"), n->data.hbar.label_right, sizeof(n->data.hbar.label_right));
        (void)parse_color(p, obj, "color", PAL_BLACK, &n->data.hbar.color);
    } else if (type_is(p, type_idx, "icon")) {
        n->kind = LN_ICON;
        copy_string(p, obj_find(p, obj, "name"), n->data.icon.name, sizeof(n->data.icon.name));
        n->data.icon.size = token_u8_default(p, obj_find(p, obj, "size"), 16);
        (void)parse_color(p, obj, "color", PAL_BLACK, &n->data.icon.color);
    } else if (type_is(p, type_idx, "image")) {
        n->kind = LN_IMAGE;
        n->data.image.width = (uint16_t)token_i16_default(p, obj_find(p, obj, "width"), 0);
        n->data.image.height = (uint16_t)token_i16_default(p, obj_find(p, obj, "height"), 0);
    } else if (type_is(p, type_idx, "sparkline")) {
        n->kind = LN_SPARKLINE;
        parse_sparkline(p, obj, n);
    } else if (type_is(p, type_idx, "marker")) {
        n->kind = LN_MARKER;
        n->data.marker.pct = pct_default_clamped(p, obj_find(p, obj, "pct"), 0);
        (void)parse_color(p, obj, "color", PAL_RED, &n->data.marker.color);
    } else {
        set_error(p, PARSE_ERR_ENUM, p->tok[type_idx].start, "unknown node type");
        return NULL;
    }

    return p->result.status == PARSE_OK ? n : NULL;
}

layout_node_t *parse_layout(const char *json, size_t json_len, arena_t *arena, parse_result_t *out_result)
{
    parse_result_t initial = {PARSE_OK, 0, "ok"};
    if (out_result != NULL) {
        *out_result = initial;
    }
    if (json == NULL || arena == NULL) {
        if (out_result != NULL) {
            out_result->status = PARSE_ERR_TYPE;
            out_result->message = "invalid argument";
        }
        return NULL;
    }

    jsmn_parser jp;
    jsmntok_t tokens[JSON_MAX_TOKENS];
    jsmn_init(&jp);
    const int count = jsmn_parse(&jp, json, json_len, tokens, JSON_MAX_TOKENS);
    if (count < 0) {
        if (out_result != NULL) {
            out_result->offset = jp.pos;
            if (count == JSMN_ERROR_NOMEM) {
                out_result->status = PARSE_ERR_TRUNCATED;
                out_result->message = "too many JSON tokens";
            } else {
                out_result->status = PARSE_ERR_JSON;
                out_result->message = "invalid JSON";
            }
        }
        return NULL;
    }
    if (count == 0 || tokens[0].type != JSMN_OBJECT) {
        if (out_result != NULL) {
            out_result->status = PARSE_ERR_TYPE;
            out_result->message = "root must be object";
        }
        return NULL;
    }

    parser_t p = {
        .json = json,
        .json_len = json_len,
        .tok = tokens,
        .tok_count = count,
        .arena = arena,
        .result = initial,
    };
    layout_node_t *root = parse_node(&p, 0);
    if (out_result != NULL) {
        *out_result = p.result;
    }
    return p.result.status == PARSE_OK ? root : NULL;
}
