#ifndef TLSR_LAYOUT_RENDER_PRIMITIVES_H
#define TLSR_LAYOUT_RENDER_PRIMITIVES_H

#include "../engine/engine.h"

void draw_divider(const laid_out_node_t *n);
void draw_text(const laid_out_node_t *n);
void draw_hbar(const laid_out_node_t *n);
void draw_marker(const laid_out_node_t *n);
void draw_sparkline(const laid_out_node_t *n);
void draw_icon(const laid_out_node_t *n);
void draw_image(const laid_out_node_t *n);

#endif
