#include "xfer.h"

#include "../proto/crc32.h"
#include "../render/pipeline.h"
#include "../store/ringlog.h"
#include "../store/slot.h"

#include <string.h>

#define XFER_EPD_BUFFER_SIZE (300 * 128 / 8)

void epd_clear_buffer(void);
void EPD_Display(unsigned char *image, unsigned char *red_image, unsigned char requested_full_refresh);
void layout_refresh_adv_payload(void);
extern uint8_t layout_status_buf[16];
uint16_t battery_read_mv(void);

#define XFER_CTL_BEGIN 0x01u
#define XFER_CTL_ABORT 0x02u
#define XFER_CTL_COMMIT 0x03u
#define XFER_DSL_FORMAT_ZLIB_JSON_V1 0x01u

extern uint8_t epd_buffer[XFER_EPD_BUFFER_SIZE];
extern uint8_t epd_buffer_red[XFER_EPD_BUFFER_SIZE];
extern void (*flash_read_page)(unsigned long addr, unsigned long len, unsigned char *buf);

typedef struct {
    xfer_state_t state;
    ack_emit_fn ack;
    slot_id_t staging;
    uint32_t data_ver;
    uint16_t total_len;
    uint16_t total_chunks;
    uint32_t crc32;
    uint8_t dsl_format;
    uint16_t recv_offset;
    uint16_t expected_seq;
} xfer_ctx_t;

static xfer_ctx_t g_xfer;

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void emit_ack(uint16_t last_seq, xfer_code_t code)
{
    if (g_xfer.ack != NULL) {
        g_xfer.ack(last_seq, (uint8_t)code, 0);
    }
}

void xfer_init(ack_emit_fn ack)
{
    memset(&g_xfer, 0, sizeof(g_xfer));
    g_xfer.state = XFER_IDLE;
    g_xfer.ack = ack;
}

xfer_state_t xfer_state(void)
{
    return g_xfer.state;
}

static int verify_staging_crc(void)
{
    uint8_t buf[256];
    uint32_t crc = 0;
    uint16_t off = 0;
    while (off < g_xfer.total_len) {
        uint16_t n = (uint16_t)(g_xfer.total_len - off);
        if (n > sizeof(buf)) {
            n = sizeof(buf);
        }
        flash_read_page(slot_addr(g_xfer.staging) + SLOT_HEADER_SIZE + off, n, buf);
        crc = crc32_zlib_update(crc, buf, n);
        off = (uint16_t)(off + n);
    }
    crc = crc32_zlib_finalize(crc);
    return crc == g_xfer.crc32 ? 0 : -1;
}

static const char *xfer_error_message(xfer_code_t code)
{
    switch (code) {
    case XFER_ERR_GAP: return "seq gap";
    case XFER_ERR_CRC: return "crc mismatch";
    case XFER_ERR_INFLATE: return "inflate failed";
    case XFER_ERR_PARSE: return "parse failed";
    case XFER_ERR_RENDER: return "render failed";
    case XFER_ERR_LOW_BATT: return "battery low";
    case XFER_ERR_BUSY: return "busy";
    case XFER_OK_PROGRESS:
    case XFER_OK_BEGIN:
    case XFER_OK_COMMIT:
    default: return "xfer error";
    }
}

static void publish_data_ver(uint32_t data_ver)
{
    layout_status_buf[0] = data_ver & 0xFF;
    layout_status_buf[1] = (data_ver >> 8) & 0xFF;
    layout_status_buf[2] = (data_ver >> 16) & 0xFF;
    layout_status_buf[3] = (data_ver >> 24) & 0xFF;
    layout_status_buf[4] = 0x01;
    layout_status_buf[5] = 0x00;
    layout_refresh_adv_payload();
}

void xfer_handle_ctl(const uint8_t *frame, uint16_t len)
{
    if (frame == NULL || len == 0) {
        return;
    }

    switch (frame[0]) {
    case XFER_CTL_BEGIN:
        if (len < 14 || g_xfer.state != XFER_IDLE) {
            emit_ack(0, XFER_ERR_BUSY);
            return;
        }
        g_xfer.data_ver = get_le32(&frame[1]);
        g_xfer.total_len = get_le16(&frame[5]);
        g_xfer.total_chunks = get_le16(&frame[7]);
        g_xfer.crc32 = get_le32(&frame[9]);
        g_xfer.dsl_format = frame[13];
        if (battery_read_mv() < XFER_MIN_BATTERY_MV) {
            ringlog_append(XFER_ERR_LOW_BATT, 0, 0, xfer_error_message(XFER_ERR_LOW_BATT));
            emit_ack(0, XFER_ERR_LOW_BATT);
            return;
        }
        if (g_xfer.total_len > SLOT_MAX_PAYLOAD || g_xfer.total_chunks > 256 ||
            g_xfer.dsl_format != XFER_DSL_FORMAT_ZLIB_JSON_V1) {
            emit_ack(0, XFER_ERR_BUSY);
            return;
        }
        g_xfer.staging = slot_staging();
        slot_erase(g_xfer.staging);
        g_xfer.recv_offset = 0;
        g_xfer.expected_seq = 0;
        g_xfer.state = XFER_RECEIVING;
        emit_ack(0, XFER_OK_BEGIN);
        return;

    case XFER_CTL_ABORT:
        g_xfer.state = XFER_IDLE;
        return;

    case XFER_CTL_COMMIT:
        if (len < 5 || g_xfer.state != XFER_RECEIVING || get_le32(&frame[1]) != g_xfer.data_ver ||
            g_xfer.recv_offset != g_xfer.total_len) {
            emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_ERR_BUSY);
            return;
        }
        if (verify_staging_crc() != 0) {
            ringlog_append(XFER_ERR_CRC, g_xfer.data_ver, 0, xfer_error_message(XFER_ERR_CRC));
            g_xfer.state = XFER_IDLE;
            emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_ERR_CRC);
            return;
        }
        g_xfer.state = XFER_COMMITTED;
        return;

    default:
        return;
    }
}

void xfer_handle_data(const uint8_t *frame, uint16_t len)
{
    if (frame == NULL || len < 2 || g_xfer.state != XFER_RECEIVING) {
        return;
    }
    const uint16_t seq = get_le16(frame);
    const uint16_t payload_len = (uint16_t)(len - 2);
    if (seq != g_xfer.expected_seq) {
        emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_ERR_GAP);
        return;
    }
    if (payload_len > g_xfer.total_len - g_xfer.recv_offset) {
        return;
    }
    if (slot_write_payload(g_xfer.staging, g_xfer.recv_offset, &frame[2], payload_len) != 0) {
        return;
    }
    g_xfer.recv_offset = (uint16_t)(g_xfer.recv_offset + payload_len);
    g_xfer.expected_seq++;
    if ((seq & 0x7u) == 0x7u || g_xfer.recv_offset == g_xfer.total_len) {
        emit_ack(seq, XFER_OK_PROGRESS);
    }
}

static xfer_code_t render_staging(uint16_t *out_uncompressed_len)
{
    uint8_t compressed[SLOT_MAX_PAYLOAD];
    uint8_t inflate_buf[4096];
    uint8_t arena_buf[4096];
    if (slot_read_payload(slot_addr(g_xfer.staging), compressed, g_xfer.total_len) != 0) {
        return XFER_ERR_RENDER;
    }

    epd_clear_buffer();
    pipeline_status_t status = pipeline_run(
        compressed,
        g_xfer.total_len,
        sizeof(inflate_buf),
        inflate_buf,
        arena_buf,
        out_uncompressed_len
    );
    if (status == PIPE_ERR_INFLATE) {
        return XFER_ERR_INFLATE;
    }
    if (status == PIPE_ERR_PARSE) {
        return XFER_ERR_PARSE;
    }
    if (status != PIPE_OK) {
        return XFER_ERR_RENDER;
    }

    EPD_Display(epd_buffer, epd_buffer_red, 1);
    return XFER_OK_COMMIT;
}

void xfer_run_pending_render(void)
{
    if (g_xfer.state != XFER_COMMITTED) {
        return;
    }

    uint16_t uncompressed_len = 0;
    xfer_code_t result = render_staging(&uncompressed_len);
    if (result != XFER_OK_COMMIT) {
        ringlog_append((uint8_t)result, g_xfer.data_ver, 0, xfer_error_message(result));
        g_xfer.state = XFER_IDLE;
        emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), result);
        return;
    }

    slot_header_t active;
    uint32_t next_seq = 1;
    if (slot_active(&active) != SLOT_NONE) {
        next_seq = active.seq + 1;
    }

    slot_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = SLOT_MAGIC_BE;
    hdr.data_ver = g_xfer.data_ver;
    hdr.uncompressed_len = uncompressed_len;
    hdr.compressed_len = g_xfer.total_len;
    hdr.crc32 = g_xfer.crc32;
    hdr.seq = next_seq;

    if (slot_write_header(g_xfer.staging, &hdr) != 0) {
        ringlog_append(XFER_ERR_RENDER, g_xfer.data_ver, 0, "header write failed");
        g_xfer.state = XFER_IDLE;
        emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_ERR_RENDER);
        return;
    }

    publish_data_ver(g_xfer.data_ver);
    emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_OK_COMMIT);
    g_xfer.state = XFER_IDLE;
}
