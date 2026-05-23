#include "paint.h"

#include "primitives.h"

static void paint_node(const laid_out_node_t *n)
{
    if (n == NULL || n->node == NULL) {
        return;
    }
    switch (n->node->kind) {
    case LN_VSTACK:
    case LN_HSTACK:
        for (uint8_t i = 0; i < n->child_count; i++) {
            paint_node(&n->children[i]);
        }
        break;
    case LN_SPACER:
        break;
    case LN_DIVIDER:
        draw_divider(n);
        break;
    case LN_TEXT:
        draw_text(n);
        break;
    case LN_HBAR:
        draw_hbar(n);
        break;
    case LN_ICON:
        draw_icon(n);
        break;
    case LN_IMAGE:
        draw_image(n);
        break;
    case LN_SPARKLINE:
        draw_sparkline(n);
        break;
    case LN_MARKER:
        draw_marker(n);
        break;
    }
}

void paint_layout(const laid_out_node_t *root)
{
    paint_node(root);
}
