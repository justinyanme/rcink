#ifndef TLSR_LAYOUT_DSL_SCHEMA_H
#define TLSR_LAYOUT_DSL_SCHEMA_H

#include <stdint.h>

#define LAYOUT_MAX_CHILDREN 32
#define LAYOUT_MAX_TEXT_LEN 128
#define LAYOUT_MAX_SPARKLINE 64

typedef enum {
    LN_VSTACK,
    LN_HSTACK,
    LN_SPACER,
    LN_DIVIDER,
    LN_TEXT,
    LN_HBAR,
    LN_ICON,
    LN_IMAGE,
    LN_SPARKLINE,
    LN_MARKER,
} layout_kind_t;

typedef enum {
    PAL_BLACK = 0,
    PAL_WHITE = 1,
    PAL_RED = 2,
    PAL_UNSET = 0xFF,
} palette_t;

typedef enum { TS_BODY = 0, TS_H1, TS_H2, TS_SM } text_size_t;
typedef enum { TW_REGULAR = 0, TW_BOLD } text_weight_t;
typedef enum { AL_START = 0, AL_CENTER, AL_END } align_t;

struct layout_node;

typedef struct {
    uint8_t gap;
    uint8_t pad;
    align_t align;
    uint8_t child_count;
    struct layout_node *children[LAYOUT_MAX_CHILDREN];
} stack_node_t;

typedef struct {
    int16_t size;
} spacer_node_t;

typedef struct {
    palette_t color;
    uint8_t thickness;
} divider_node_t;

typedef struct {
    char value[LAYOUT_MAX_TEXT_LEN];
    uint8_t value_len;
    text_size_t size;
    palette_t color;
    palette_t bg;
    text_weight_t weight;
    align_t align;
    uint8_t pad;
} text_node_t;

typedef struct {
    int16_t pct;
    palette_t color;
    uint8_t height;
    char label_left[24];
    char label_right[24];
} hbar_node_t;

typedef struct {
    char name[16];
    uint8_t size;
    palette_t color;
} icon_node_t;

typedef struct {
    uint16_t width;
    uint16_t height;
} image_node_t;

typedef struct {
    int16_t values[LAYOUT_MAX_SPARKLINE];
    uint8_t value_count;
    palette_t color;
    uint8_t height;
} sparkline_node_t;

typedef struct {
    int16_t pct;
    palette_t color;
} marker_node_t;

typedef struct layout_node {
    layout_kind_t kind;
    union {
        stack_node_t stack;
        spacer_node_t spacer;
        divider_node_t divider;
        text_node_t text;
        hbar_node_t hbar;
        icon_node_t icon;
        image_node_t image;
        sparkline_node_t sparkline;
        marker_node_t marker;
    };
} layout_node_t;

#endif
