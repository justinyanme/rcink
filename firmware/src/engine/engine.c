#include "engine.h"

#include "measure.h"

#include <stdbool.h>
#include <stddef.h>

static int16_t max16(int16_t a, int16_t b)
{
    return a > b ? a : b;
}

static int16_t clamp_nonnegative_i32(int32_t v)
{
    if (v < 0) {
        return 0;
    }
    if (v > INT16_MAX) {
        return INT16_MAX;
    }
    return (int16_t)v;
}

static bool layout_node_into(const layout_node_t *node, rect_t vp, const measurer_t *m, arena_t *arena, laid_out_node_t *out);

static bool child_is_flex_main(const layout_node_t *child, bool vertical)
{
    if (child == NULL) {
        return false;
    }
    if (child->kind == LN_SPACER && child->data.spacer.size < 0) {
        return true;
    }
    if (!vertical && (child->kind == LN_HBAR || child->kind == LN_SPARKLINE || child->kind == LN_DIVIDER)) {
        return true;
    }
    return false;
}

static size_t16_t used_size_from_stack(const laid_out_node_t *laid, bool parent_vertical)
{
    int16_t used_main = 0;
    int16_t used_cross = 0;
    for (uint8_t i = 0; i < laid->child_count; i++) {
        const laid_out_node_t *ch = &laid->children[i];
        const int16_t end_main = parent_vertical
            ? (int16_t)(ch->rect.y - laid->rect.y + ch->rect.h)
            : (int16_t)(ch->rect.x - laid->rect.x + ch->rect.w);
        const int16_t end_cross = parent_vertical
            ? (int16_t)(ch->rect.x - laid->rect.x + ch->rect.w)
            : (int16_t)(ch->rect.y - laid->rect.y + ch->rect.h);
        used_main = max16(used_main, end_main);
        used_cross = max16(used_cross, end_cross);
    }
    return parent_vertical ? (size_t16_t){used_cross, used_main} : (size_t16_t){used_main, used_cross};
}

static bool layout_stack_into(const layout_node_t *root, rect_t viewport, const measurer_t *m, arena_t *arena, laid_out_node_t *out)
{
    const bool vertical = root->kind == LN_VSTACK;
    const stack_node_t *s = &root->data.stack;
    const uint8_t n_children = s->child_count;

    out->node = root;
    out->rect = viewport;
    out->child_count = n_children;
    out->children = NULL;

    if (n_children > 0) {
        out->children = arena_alloc(arena, sizeof(laid_out_node_t) * n_children, sizeof(void *));
        if (out->children == NULL) {
            return false;
        }
    }

    rect_t inner = {
        (int16_t)(viewport.x + s->pad),
        (int16_t)(viewport.y + s->pad),
        (int16_t)(viewport.w - 2 * s->pad),
        (int16_t)(viewport.h - 2 * s->pad),
    };
    if (inner.w < 0) {
        inner.w = 0;
    }
    if (inner.h < 0) {
        inner.h = 0;
    }

    const int16_t cross_avail = vertical ? inner.w : inner.h;
    const int16_t main_avail = vertical ? inner.h : inner.w;

    int16_t child_main[LAYOUT_MAX_CHILDREN] = {0};
    int16_t child_cross[LAYOUT_MAX_CHILDREN] = {0};
    bool is_flex[LAYOUT_MAX_CHILDREN] = {false};
    uint8_t flex_count = 0;
    int16_t fixed_main = 0;
    int16_t max_cross = 0;

    for (uint8_t i = 0; i < n_children; i++) {
        const layout_node_t *c = s->children[i];
        if (child_is_flex_main(c, vertical)) {
            flex_count++;
            is_flex[i] = true;
            if (c->kind == LN_HBAR) {
                child_cross[i] = c->data.hbar.height == 0 ? 8 : c->data.hbar.height;
            } else if (c->kind == LN_SPARKLINE) {
                child_cross[i] = c->data.sparkline.height == 0 ? 8 : c->data.sparkline.height;
            } else if (c->kind == LN_DIVIDER) {
                child_cross[i] = c->data.divider.thickness == 0 ? 1 : c->data.divider.thickness;
            }
            if (child_cross[i] > 0) {
                max_cross = max16(max_cross, child_cross[i]);
            }
            continue;
        }

        size_t16_t sz;
        if (c->kind == LN_VSTACK || c->kind == LN_HSTACK) {
            rect_t probe = vertical
                ? (rect_t){0, 0, cross_avail, INT16_MAX}
                : (rect_t){0, 0, INT16_MAX, cross_avail};
            laid_out_node_t laid = {0};
            if (!layout_stack_into(c, probe, m, arena, &laid)) {
                return false;
            }
            sz = used_size_from_stack(&laid, vertical);
        } else {
            sz = measure_natural(c, m, cross_avail, vertical);
        }

        child_main[i] = vertical ? sz.h : sz.w;
        child_cross[i] = vertical ? sz.w : sz.h;
        fixed_main = (int16_t)(fixed_main + child_main[i]);
        max_cross = max16(max_cross, child_cross[i]);
    }

    const int16_t gaps_total = n_children > 1 ? (int16_t)((n_children - 1) * s->gap) : 0;
    const bool unbounded = main_avail >= LAYOUT_UNBOUNDED;
    int16_t flex_each = 0;
    if (flex_count > 0 && !unbounded) {
        flex_each = (int16_t)(clamp_nonnegative_i32((int32_t)main_avail - fixed_main - gaps_total) / flex_count);
    }

    int16_t cursor = vertical ? inner.y : inner.x;
    for (uint8_t i = 0; i < n_children; i++) {
        const layout_node_t *c = s->children[i];
        int16_t main_sz;
        int16_t cross_sz;
        if (is_flex[i]) {
            main_sz = flex_each;
            cross_sz = child_cross[i] > 0 ? child_cross[i] : max_cross;
        } else {
            main_sz = child_main[i];
            cross_sz = child_cross[i] > 0 ? child_cross[i] : cross_avail;
        }

        const int16_t cross_base = vertical ? inner.x : inner.y;
        int16_t cross_pos = cross_base;
        const int16_t ref_cross = max16(max_cross, cross_sz);
        if (s->align == AL_CENTER) {
            cross_pos = (int16_t)(cross_base + (ref_cross - cross_sz) / 2);
        } else if (s->align == AL_END) {
            cross_pos = (int16_t)(cross_base + (ref_cross - cross_sz));
        }

        rect_t child_vp = vertical
            ? (rect_t){cross_pos, cursor, cross_sz, main_sz}
            : (rect_t){cursor, cross_pos, main_sz, cross_sz};
        if (!layout_node_into(c, child_vp, m, arena, &out->children[i])) {
            return false;
        }

        cursor = (int16_t)(cursor + main_sz);
        if (i + 1 < n_children) {
            cursor = (int16_t)(cursor + s->gap);
        }
    }
    return true;
}

static bool layout_node_into(const layout_node_t *node, rect_t vp, const measurer_t *m, arena_t *arena, laid_out_node_t *out)
{
    if (node == NULL || out == NULL) {
        return false;
    }
    if (node->kind == LN_VSTACK || node->kind == LN_HSTACK) {
        return layout_stack_into(node, vp, m, arena, out);
    }
    out->node = node;
    out->rect = vp;
    out->child_count = 0;
    out->children = NULL;
    return true;
}

laid_out_node_t *run_layout(const layout_node_t *root, rect_t viewport, const measurer_t *m, arena_t *arena)
{
    if (root == NULL || arena == NULL) {
        return NULL;
    }
    laid_out_node_t *out = arena_alloc(arena, sizeof(*out), sizeof(void *));
    if (out == NULL) {
        return NULL;
    }
    if (!layout_node_into(root, viewport, m, arena, out)) {
        return NULL;
    }
    return out;
}
