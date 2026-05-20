#include "ble_service.h"

#include <string.h>

#include <app_att.h>
#include <app_config.h>
#include <log.h>
#include <epd/epd_spi.h>

#include "tl_common.h"
#include "stack/ble/ble.h"

#include "epd/epd.h"
#include "ble.h"

#include "etime.h"
#include "compress.h"

#define ATT_WRITE_HEADER_BYTES 3

extern char text[MAX_TEXT_SIZE];

static u16 buffer_cursor = 0;
static u8 buffer[EPD_BUFFER_SIZE];
extern u8 epd_buffer[EPD_BUFFER_SIZE];

static bool refresh_mode_is_valid(const u8 mode)
{
    return mode <= EPD_RM_PARTIAL;
}

static bool color_is_valid(const u8 color)
{
    return color <= EPD_COLOR_RED;
}

static bool scene_is_valid(const u8 scene)
{
    return scene <= EPD_SCENE_IMAGE;
}

static bool read_refresh_mode(
    const u8 *payload, const u16 payload_len, const EPD_REFRESH_MODE fallback, EPD_REFRESH_MODE *out
) {
    if (payload_len == 0) {
        *out = fallback;
        return true;
    }
    if (!refresh_mode_is_valid(payload[0])) {
        log_debugf("invalid refresh mode: %d", payload[0]);
        return false;
    }
    *out = (EPD_REFRESH_MODE)payload[0];
    return true;
}

static bool append_buffer_chunk(const u8 *payload, const u16 payload_len)
{
    if (payload_len == 0) {
        log_debugf("empty buffer chunk");
        return false;
    }
    if (buffer_cursor > EPD_BUFFER_SIZE || payload_len > EPD_BUFFER_SIZE - buffer_cursor) {
        log_debugf(
            "buffer upload overflow: cursor=%d chunk=%d capacity=%d",
            buffer_cursor, payload_len, EPD_BUFFER_SIZE
        );
        buffer_cursor = 0;
        return false;
    }
    memcpy(&buffer[buffer_cursor], payload, payload_len);
    buffer_cursor += payload_len;
    return true;
}

static void fill_frame_tail(u8 *frame, const u16 frame_len, const bool is_red)
{
    if (frame_len < EPD_FRAME_BYTES) {
        memset(&frame[frame_len], is_red ? 0x00 : 0xff, EPD_FRAME_BYTES - frame_len);
    }
}

int layout_xfer_ctl_write(void *p)
{
    if (p == NULL) {
        return 1;
    }
    rf_packet_att_write_t *req = (rf_packet_att_write_t*)p;
    u16 len = req->l2capLen > ATT_WRITE_HEADER_BYTES ? req->l2capLen - ATT_WRITE_HEADER_BYTES : 0;
    if (len > sizeof(layout_xfer_ctl_scratch)) {
        len = sizeof(layout_xfer_ctl_scratch);
    }
    memcpy(layout_xfer_ctl_scratch, &req->value, len);
    if (len > 0) {
        log_debugf("LAYOUT XFER_CTL cmd=0x%x len=%d", req->value, len);
    } else {
        log_debugf("LAYOUT XFER_CTL empty write");
    }
    return 0;
}

int layout_xfer_data_write(void *p)
{
    if (p == NULL) {
        return 1;
    }
    rf_packet_att_write_t *req = (rf_packet_att_write_t*)p;
    u16 len = req->l2capLen > ATT_WRITE_HEADER_BYTES ? req->l2capLen - ATT_WRITE_HEADER_BYTES : 0;
    if (len > sizeof(layout_xfer_data_scratch)) {
        len = sizeof(layout_xfer_data_scratch);
    }
    memcpy(layout_xfer_data_scratch, &req->value, len);
    log_debugf("LAYOUT XFER_DATA chunk len=%d", len);
    return 0;
}

int cmd_handler(void *p) {
    if (p == NULL) {
        return 1;
    }

    rf_packet_att_write_t *req = (rf_packet_att_write_t*)p;
    if (req->l2capLen <= ATT_WRITE_HEADER_BYTES) {
        log_debugf("BLE write missing command payload: l2capLen=%d", req->l2capLen);
        return 1;
    }

    const u16 raw_payload_len = req->l2capLen - ATT_WRITE_HEADER_BYTES;
    log_debugf("ble service[%d]: type:%d, rf_len:%d, l2capLen:%d, chanId:%d, opcode:%d",
        raw_payload_len, req->type, req->rf_len, req->l2capLen, req->chanId, req->opcode);

    u8 *raw_payload = &req->value;

    tlkapi_send_string_data(APP_LOG_EN, "ble service raw data", raw_payload, raw_payload_len);

    const CMD_TYPE typ = (CMD_TYPE)raw_payload[0];

    const u8 *payload = raw_payload + 1;
    const u16 payload_len = raw_payload_len - 1;

    switch (typ) {
    case CMD_T_NONE: return 0;
    case CMD_T_LAYOUT_BEGIN:
        {
            log_debugf("CMD_T_LAYOUT_BEGIN len=%d", raw_payload_len);
            return 0;
        }
    case CMD_T_LAYOUT_ABORT:
        {
            log_debugf("CMD_T_LAYOUT_ABORT");
            return 0;
        }
    case CMD_T_LAYOUT_COMMIT:
        {
            log_debugf("CMD_T_LAYOUT_COMMIT len=%d", raw_payload_len);
            return 0;
        }
    case CMD_T_SYNC_TIME:
        {
            if (payload_len < 9) {
                log_debugf("sync time requires 9 payload bytes, got %d", payload_len);
                return 1;
            }
            // [0~3]: timestamp
            const u32 new_time = ((u32)payload[0] << 24) | ((u32)payload[1] << 16) |
                ((u32)payload[2] << 8) | payload[3];
            const u16 year = ((u16)payload[4] << 8) | payload[5];
            set_time(new_time, year, payload[6], payload[7], payload[8]);
            return 0;
        }
    case CMD_T_CHANGE_SCENE:
        {
            if (payload_len < 1 || !scene_is_valid(payload[0])) {
                log_debugf("invalid scene payload: len=%d scene=%d", payload_len, payload_len > 0 ? payload[0] : 0xff);
                return 1;
            }
            change_scene((EPD_SCENE)payload[0]);
            return 0;
        }
    case CMD_T_FORCE_REFRESH:
        {
            EPD_REFRESH_MODE refresh_mode = EPD_RM_PARTIAL;
            if (!read_refresh_mode(payload, payload_len, EPD_RM_PARTIAL, &refresh_mode)) {
                return 1;
            }
            set_force_refresh_when_available(refresh_mode);
            return 0;
        }
    case CMD_T_SET_TEXT:
        {
            // [1...payload_len] its the text, set it to display list
            const u8 len = min(payload_len, MAX_TEXT_SIZE - 1);
            memcpy(text, payload, len);
            text[len] = '\0';
            log_debugf("display list: '%s'", text);
            return 0;
        }
    // recv epd_buffer
    case CMD_T_BUF:
        buffer_cursor = 0;
    // fallthrough
    case CMD_T_BUF_CONT:
        {
            return append_buffer_chunk(payload, payload_len) ? 0 : 1;
        }
    case CMD_T_WRITE_RAM:
        {
            const bool is_red = payload_len > 0 ? payload[0] != 0 : false;
            const COMPRESS_ALGORITHM compress_algorithm = payload_len > 1 ?
                (COMPRESS_ALGORITHM)payload[1] : COMPRESS_ALG_NONE;
            if (compress_algorithm > COMPRESS_ALG_SUPPORTED_MAX) {
                log_debugf("WARNING: compress algorithm not supported: %d", compress_algorithm);
                return 1;
            }
            if (buffer_cursor == 0) {
                log_debugf("write ram requested before buffer upload");
                return 1;
            }
            log_debugf("write ram: is_red:%d, compress_algorithm:%d", is_red, compress_algorithm);
            switch (compress_algorithm) {
            case COMPRESS_ALG_NONE:
                fill_frame_tail(buffer, buffer_cursor, is_red);
                EPD_WriteRam(buffer, EPD_WIDTH, EPD_HEIGHT, 0, 0, is_red);
                return 0;
            case COMPRESS_ALG_BINARY_RLE:
                {
                    const u16 decode_len = binary_rle_decode(buffer, buffer_cursor, epd_buffer, EPD_BUFFER_SIZE);
                    log_debugf("buffer_cursor: %d, decode_len: %d", buffer_cursor, decode_len);
                    if (decode_len == 0) {
                        log_debugf("binary RLE decode failed");
                        return 1;
                    }
                    fill_frame_tail(epd_buffer, decode_len, is_red);
                    EPD_WriteRam(epd_buffer, EPD_WIDTH, EPD_HEIGHT, 0, 0, is_red);
                }
                return 0;
            case COMPRESS_ALG_LZ4:
                {
                    const u64 decode_len = lz4dec(buffer, epd_buffer, buffer_cursor, EPD_BUFFER_SIZE);
                    log_debugf("buffer_cursor: %d, decode_len: %d", buffer_cursor, (u16)decode_len);
                    if (decode_len == 0 || decode_len > EPD_BUFFER_SIZE) {
                        log_debugf("LZ4 decode failed: %d", (u16)decode_len);
                        return 1;
                    }
                    fill_frame_tail(epd_buffer, (u16)decode_len, is_red);
                    EPD_WriteRam(epd_buffer, EPD_WIDTH, EPD_HEIGHT, 0, 0, is_red);
                }
                return 0;
            default:
                log_debugf("WARNING: unknown compress algorithm: %d", compress_algorithm);
                return 1;
            }
        }
    case CMD_T_FILL_RAM:
        {
            if (payload_len > 0 && !color_is_valid(payload[0])) {
                log_debugf("invalid fill color: %d", payload[0]);
                return 1;
            }
            const EPD_COLOR color = payload_len > 0 ? (EPD_COLOR)payload[0] : EPD_COLOR_WHITE;
            epd_fill_ram(color);
            return 0;
        }
    case CMD_T_TRIGGER_DISPLAY:
        {
            EPD_REFRESH_MODE mode = EPD_RM_FULL;
            if (!read_refresh_mode(payload, payload_len, EPD_RM_FULL, &mode)) {
                return 1;
            }
            EPD_Trigger_Display(mode);
            return 0;
        }
    default:
        log_debugf("WARNING: unknown command type: %d", typ);
        return 1;
    }
}
