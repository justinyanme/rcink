#include "host_runtime.h"

#include "../src/dsl/arena.h"
#include "../src/dsl/parser.h"

#include <stdio.h>
#include <string.h>

int text_dy_test(void)
{
    static uint8_t parse_buf[1024];
    arena_t arena;
    arena_init(&arena, parse_buf, sizeof(parse_buf));

    const char *json = "{\"type\":\"text\",\"value\":\"hi\",\"pad\":2,\"dy\":-3}";
    parse_result_t pr;
    layout_node_t *root = parse_layout(json, strlen(json), &arena, &pr);
    if (root == NULL || root->kind != LN_TEXT) {
        fprintf(stderr, "text-dy: parse failed (%d)\n", pr.status);
        return 1;
    }
    if (root->data.text.pad != 2 || root->data.text.dy != -3) {
        fprintf(stderr, "text-dy: values wrong pad=%u dy=%d\n",
                (unsigned)root->data.text.pad,
                (int)root->data.text.dy);
        return 1;
    }

    arena_init(&arena, parse_buf, sizeof(parse_buf));
    json = "{\"type\":\"text\",\"value\":\"hi\"}";
    root = parse_layout(json, strlen(json), &arena, &pr);
    if (root == NULL || root->kind != LN_TEXT || root->data.text.dy != 0) {
        fprintf(stderr, "text-dy: default wrong\n");
        return 1;
    }
    puts("OK text-dy");
    return 0;
}
