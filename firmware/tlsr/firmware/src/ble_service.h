#pragma once

// NOTE: this could be generated from json to c and python code with cmake
typedef enum {
    CMD_T_NONE = 0x00,
    // Layout-DSL commands. Wire protocol is defined in docs/tlsr-ble-protocol.md.
    CMD_T_LAYOUT_BEGIN = 0x01,
    CMD_T_LAYOUT_ABORT = 0x02,
    CMD_T_LAYOUT_COMMIT = 0x03,

    CMD_T_SET_TEXT = 0x04, // set text for display, args: [u8] // ascii text

    CMD_T_BUF = 0x05, // start receive buffer data, reset cursor, args: [u8]
    CMD_T_BUF_CONT = 0x06, // receive buffer data continue from last cursor, args: [u8]
    CMD_T_WRITE_RAM = 0x07, // write buffer to ram BW/RED, args: [is_red=FALSE, compress_algorithm=NONE]
    CMD_T_FILL_RAM = 0x08, // fill ram with color, args: [EPD_COLOR], default EPD_COLOR_WHITE
    CMD_T_TRIGGER_DISPLAY = 0x10, // trigger display, args: [EPD_REFRESH_MODE], default EPD_RM_FULL

    CMD_T_SYNC_TIME = 0xDD,
    CMD_T_CHANGE_SCENE = 0xE1,
    CMD_T_FORCE_REFRESH = 0xE2,
} CMD_TYPE;

typedef enum {
    COMPRESS_ALG_NONE = 0x00,
    COMPRESS_ALG_BINARY_RLE = 0x01, // custom rle, only for some whitelist bytes
    COMPRESS_ALG_LZ4 = 0x02,
    COMPRESS_ALG_SUPPORTED_MAX = COMPRESS_ALG_LZ4,
} COMPRESS_ALGORITHM;

int cmd_handler(void *p);
void layout_xfer_service_init(void);
void layout_xfer_service_tick(void);
int layout_xfer_ctl_write(void *p);
int layout_xfer_data_write(void *p);
