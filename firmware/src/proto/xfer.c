#include "xfer.h"

#include "../dsl/arena.h"
#include "../dsl/parser.h"
#include "../engine/engine.h"
#include "../engine/measure.h"
#include "../render/paint.h"
#include "../store/slot.h"
#define UZLIB_SKIP_STDLIB 1
#include "uzlib/uzlib.h"

#include <string.h>

#define XFER_EPD_WIDTH 250
#define XFER_EPD_HEIGHT 128
#define XFER_EPD_BUFFER_SIZE (300 * 128 / 8)

uint32_t crc32_zlib(const uint8_t *data, unsigned int len);
void epd_clear_buffer(void);
void EPD_Display(unsigned char *image, unsigned char *red_image, unsigned char requested_full_refresh);

#define XFER_CTL_BEGIN 0x01u
#define XFER_CTL_ABORT 0x02u
#define XFER_CTL_COMMIT 0x03u
#define XFER_DSL_FORMAT_ZLIB_JSON_V1 0x01u

extern uint8_t epd_buffer[XFER_EPD_BUFFER_SIZE];
extern uint8_t epd_buffer_red[XFER_EPD_BUFFER_SIZE];

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

static size_t16_t device_text(const measurer_t *m, const text_node_t *t)
{
    (void)m;
    return (size_t16_t){
        (int16_t)(t->value_len * 7 + 2 * t->pad),
        (int16_t)(layout_font_height(t->size) + 2 * t->pad),
    };
}

static size_t16_t device_icon(const measurer_t *m, const icon_node_t *i)
{
    (void)m;
    const int16_t s = i->size == 0 ? 16 : i->size;
    return (size_t16_t){s, s};
}

static size_t16_t device_image(const measurer_t *m, const image_node_t *i)
{
    (void)m;
    return (size_t16_t){(int16_t)i->width, (int16_t)i->height};
}

static const measurer_t measurer_device = {
    .text = device_text,
    .icon = device_icon,
    .image = device_image,
    .user = 0,
};

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
    uint8_t compressed[SLOT_MAX_PAYLOAD];
    if (slot_read_payload(slot_addr(g_xfer.staging), compressed, g_xfer.total_len) != 0) {
        return -1;
    }
    return crc32_zlib(compressed, g_xfer.total_len) == g_xfer.crc32 ? 0 : -1;
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
    if (seq != g_xfer.expected_seq || payload_len > g_xfer.total_len - g_xfer.recv_offset) {
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

    struct uzlib_uncomp ctx;
    memset(&ctx, 0, sizeof(ctx));
    uzlib_init();
    uzlib_uncompress_init(&ctx, NULL, 0);
    ctx.source = compressed;
    ctx.source_limit = compressed + g_xfer.total_len;
    ctx.source_read_cb = NULL;
    ctx.dest_start = inflate_buf;
    ctx.dest = inflate_buf;
    ctx.dest_limit = inflate_buf + sizeof(inflate_buf);
    if (uzlib_zlib_parse_header(&ctx) < 0 || uzlib_uncompress_chksum(&ctx) != TINF_DONE) {
        return XFER_ERR_INFLATE;
    }
    const uint16_t uncompressed_len = (uint16_t)(ctx.dest - ctx.dest_start);

    arena_t arena;
    arena_init(&arena, arena_buf, sizeof(arena_buf));
    parse_result_t pr;
    layout_node_t *root = parse_layout((const char *)inflate_buf, uncompressed_len, &arena, &pr);
    if (root == NULL || pr.status != PARSE_OK) {
        return XFER_ERR_PARSE;
    }

    laid_out_node_t *laid = run_layout(root, (rect_t){0, 0, XFER_EPD_WIDTH, XFER_EPD_HEIGHT}, &measurer_device, &arena);
    if (laid == NULL) {
        return XFER_ERR_RENDER;
    }

    epd_clear_buffer();
    paint_layout(laid);
    EPD_Display(epd_buffer, epd_buffer_red, 1);
    *out_uncompressed_len = uncompressed_len;
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
        g_xfer.state = XFER_IDLE;
        emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_ERR_RENDER);
        return;
    }

    emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_OK_COMMIT);
    g_xfer.state = XFER_IDLE;
}
