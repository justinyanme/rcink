#include "host_canvas.h"

#include "../src/dsl/arena.h"
#include "../src/dsl/parser.h"
#include "../src/engine/engine.h"
#include "../src/render/paint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const measurer_t host_measurer;

static const char *kind_name(layout_kind_t kind)
{
    switch (kind) {
    case LN_VSTACK: return "vstack";
    case LN_HSTACK: return "hstack";
    case LN_SPACER: return "spacer";
    case LN_DIVIDER: return "divider";
    case LN_TEXT: return "text";
    case LN_HBAR: return "hbar";
    case LN_ICON: return "icon";
    case LN_IMAGE: return "image";
    case LN_SPARKLINE: return "sparkline";
    case LN_MARKER: return "marker";
    }
    return "unknown";
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    *len = (size_t)n;
    return buf;
}

static void emit_layout_node(const laid_out_node_t *n, int *first)
{
    if (!*first) {
        printf(",");
    }
    *first = 0;
    printf("{\"kind\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",
           kind_name(n->node->kind), n->rect.x, n->rect.y, n->rect.w, n->rect.h);
    for (uint8_t i = 0; i < n->child_count; i++) {
        emit_layout_node(&n->children[i], first);
    }
}

static int load_and_layout(const char *path, arena_t *parse_arena, arena_t *layout_arena, laid_out_node_t **out)
{
    size_t json_len = 0;
    char *json = read_file(path, &json_len);
    if (json == NULL) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }
    parse_result_t result;
    layout_node_t *root = parse_layout(json, json_len, parse_arena, &result);
    free(json);
    if (root == NULL) {
        fprintf(stderr, "parse error %d at %d: %s\n", result.status, result.offset, result.message);
        return 1;
    }
    *out = run_layout(root, (rect_t){0, 0, 250, 128}, &host_measurer, layout_arena);
    if (*out == NULL) {
        fprintf(stderr, "layout failed\n");
        return 1;
    }
    return 0;
}

static const char *arg_value(int argc, char **argv, const char *name)
{
    for (int i = 3; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "version") == 0) {
        puts("tlsr-layout-host-test phase3");
        return 0;
    }
    if (argc < 3) {
        fprintf(stderr, "usage: host_test layout <scenario.json> | render <scenario.json> --pgm out.pgm [--png out.png]\n");
        return 1;
    }

    uint8_t parse_buf[65536];
    uint8_t layout_buf[65536];
    arena_t parse_arena;
    arena_t layout_arena;
    arena_init(&parse_arena, parse_buf, sizeof(parse_buf));
    arena_init(&layout_arena, layout_buf, sizeof(layout_buf));

    laid_out_node_t *laid = NULL;
    if (load_and_layout(argv[2], &parse_arena, &layout_arena, &laid) != 0) {
        return 1;
    }

    if (strcmp(argv[1], "layout") == 0) {
        int first = 1;
        printf("[");
        emit_layout_node(laid, &first);
        printf("]\n");
        return 0;
    }
    if (strcmp(argv[1], "render") == 0) {
        const char *pgm = arg_value(argc, argv, "--pgm");
        const char *png = arg_value(argc, argv, "--png");
        if (pgm == NULL) {
            fprintf(stderr, "render requires --pgm\n");
            return 1;
        }
        host_canvas_init(250, 128);
        host_canvas_clear(1);
        paint_layout(laid);
        if (host_canvas_write_pgm(pgm) != 0) {
            fprintf(stderr, "failed to write %s\n", pgm);
            return 1;
        }
        if (png != NULL && host_canvas_write_png(png) != 0) {
            fprintf(stderr, "failed to write %s\n", png);
            return 1;
        }
        return 0;
    }
    fprintf(stderr, "unknown command: %s\n", argv[1]);
    return 1;
}
