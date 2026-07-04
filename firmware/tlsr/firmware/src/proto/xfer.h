#ifndef TLSR_LAYOUT_PROTO_XFER_H
#define TLSR_LAYOUT_PROTO_XFER_H

#include <stdint.h>

#include "wire.h"

#define XFER_MIN_BATTERY_MV 2400u

typedef enum {
    XFER_IDLE,
    XFER_RECEIVING,
    XFER_COMMITTED,
} xfer_state_t;

typedef enum {
    XFER_OK_PROGRESS = 0x00,
    XFER_OK_BEGIN = 0x01,
    XFER_OK_COMMIT = 0x02,
    XFER_ERR_GAP = 0x10,
    XFER_ERR_CRC = 0x11,
    XFER_ERR_INFLATE = 0x12,
    XFER_ERR_PARSE = 0x13,
    XFER_ERR_RENDER = 0x14,
    XFER_ERR_TOO_BIG = 0x15,
    XFER_ERR_FORMAT = 0x16,
    XFER_ERR_OVERRUN = 0x17,
    XFER_ERR_FLASH = 0x18,
    XFER_ERR_LOW_BATT = 0x20,
    XFER_ERR_BUSY = 0x21,
} xfer_code_t;

typedef void (*ack_emit_fn)(uint16_t last_seq, uint8_t code, uint8_t flags);

void xfer_init(ack_emit_fn ack);
void xfer_handle_ctl(const uint8_t *frame, uint16_t len);
void xfer_handle_data(const uint8_t *frame, uint16_t len);
void xfer_run_pending_render(void);
/* Request a one-shot redraw of the stored active slot (call on cold boot);
 * the render runs from the main loop via xfer_run_boot_render(). */
void xfer_request_boot_render(void);
void xfer_run_boot_render(void);
void xfer_tick(uint32_t now_ms);
xfer_state_t xfer_state(void);
uint8_t xfer_adv_flags(void);

#endif
