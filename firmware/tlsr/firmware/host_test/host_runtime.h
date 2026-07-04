#ifndef TLSR_LAYOUT_HOST_TEST_RUNTIME_H
#define TLSR_LAYOUT_HOST_TEST_RUNTIME_H

#include "../src/epd/scene.h"

#include <stdint.h>

typedef struct {
    uint16_t last_seq;
    uint8_t code;
    uint8_t flags;
} host_ack_t;

extern uint8_t layout_status_buf[16];
extern uint16_t current_battery_mv;

extern uint32_t g_host_now_ms;
extern uint16_t g_host_battery_mv;
extern uint8_t g_host_epd_display_ok;
extern uint8_t g_host_epd_display_busy;
extern uint8_t g_host_epd_display_count;
extern uint8_t g_host_fix_buffer_count;
extern EPD_SCENE g_host_scene;
extern host_ack_t g_acks[16];
extern uint8_t g_ack_count;

void host_xfer_reset(void);

#endif
