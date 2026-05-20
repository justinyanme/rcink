#include "measure.h"

int16_t layout_font_height(text_size_t s)
{
    switch (s) {
    case TS_H1:
        return 28;
    case TS_H2:
        return 22;
    case TS_BODY:
        return 14;
    case TS_SM:
        return 10;
    }
    return 14;
}

static uint8_t nonzero_u8(uint8_t value, uint8_t fallback)
{
    return value == 0 ? fallback : value;
}

size_t16_t measure_natural(const layout_node_t *node, const measurer_t *m, int16_t parent_cross, bool vertical)
{
    size_t16_t zero = {0, 0};
    if (node == NULL) {
        return zero;
    }

    switch (node->kind) {
    case LN_TEXT:
        if (m != NULL && m->text != NULL) {
            return m->text(m, &node->data.text);
        }
        return (size_t16_t){(int16_t)(node->data.text.value_len * 7 + 2 * node->data.text.pad),
                            (int16_t)(layout_font_height(node->data.text.size) + 2 * node->data.text.pad)};
    case LN_ICON:
        if (m != NULL && m->icon != NULL) {
            return m->icon(m, &node->data.icon);
        }
        return (size_t16_t){node->data.icon.size, node->data.icon.size};
    case LN_IMAGE:
        if (m != NULL && m->image != NULL) {
            return m->image(m, &node->data.image);
        }
        return (size_t16_t){(int16_t)node->data.image.width, (int16_t)node->data.image.height};
    case LN_SPACER:
        if (node->data.spacer.size >= 0) {
            return (size_t16_t){node->data.spacer.size, node->data.spacer.size};
        }
        return zero;
    case LN_DIVIDER: {
        const int16_t thickness = nonzero_u8(node->data.divider.thickness, 1);
        return vertical ? (size_t16_t){parent_cross, thickness} : (size_t16_t){thickness, parent_cross};
    }
    case LN_HBAR: {
        const int16_t h = nonzero_u8(node->data.hbar.height, 8);
        return vertical ? (size_t16_t){parent_cross, h} : (size_t16_t){h, parent_cross};
    }
    case LN_SPARKLINE: {
        const int16_t h = nonzero_u8(node->data.sparkline.height, 8);
        return vertical ? (size_t16_t){parent_cross, h} : (size_t16_t){h, parent_cross};
    }
    case LN_MARKER:
        return vertical ? (size_t16_t){parent_cross, 4} : (size_t16_t){1, parent_cross};
    case LN_VSTACK:
    case LN_HSTACK:
    default:
        return zero;
    }
}
