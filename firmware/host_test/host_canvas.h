#ifndef TLSR_LAYOUT_HOST_CANVAS_H
#define TLSR_LAYOUT_HOST_CANVAS_H

#include <stdint.h>

void host_canvas_init(int16_t w, int16_t h);
void host_canvas_clear(uint8_t color);
void *host_canvas_buffer(int16_t *w, int16_t *h);
int host_canvas_write_png(const char *path);
int host_canvas_write_pgm(const char *path);

#endif
