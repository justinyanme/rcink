#pragma once

// NOTE: this could be generated from json to c and python code with cmake
typedef enum {
    CMD_T_NONE = 0x00,
    CMD_T_CLEAR = 0x01, // clear display // this will implement with fill+trigger, args: [EPD_COLOR], default EPD_COLOR_WHITE
    CMD_T_RESET = 0x02, // reset + setup

    CMD_T_SET_TEXT = 0x04, // set text for display, args: [u8] // ascii text

    CMD_T_BUF = 0x05, // start receive buffer data, reset cursor, args: [u8]
    CMD_T_BUF_CONT = 0x06, // receive buffer data continue from last cursor, args: [u8]
    CMD_T_WRITE_RAM = 0x07, // write buffer to ram BW/RED, args: [is_red=FALSE, compress_algorithm=NONE]
    CMD_T_FILL_RAM = 0x08, // fill ram with color, args: [EPD_COLOR], default EPD_COLOR_WHITE
    CMD_T_TRIGGER_DISPLAY = 0x10, // trigger display, args: [EPD_REFRESH_MODE], default EPD_RM_FULL

    CMD_T_QUOTE_BEGIN = 0x20, // begin runtime quote upload, args: [payload_len_le:u16, quote_count:u8]
    CMD_T_QUOTE_DATA = 0x21, // append runtime quote bytes, payload is NUL-separated UTF-8 quote data
    CMD_T_QUOTE_COMMIT = 0x22, // validate + persist runtime quote upload, args: [EPD_REFRESH_MODE], default EPD_RM_FULL
    CMD_T_QUOTE_CLEAR = 0x23, // erase runtime quote upload and fall back to compiled quotes

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
