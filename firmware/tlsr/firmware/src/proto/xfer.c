#include "xfer.h"

#include "../proto/crc32.h"
#include "../epd/scene.h"
#include "../render/pipeline.h"
#include "../store/ringlog.h"
#include "../store/slot.h"

#include <string.h>

#define XFER_EPD_BUFFER_SIZE (300 * 128 / 8)

void epd_clear_buffer(void);
void FixBuffer(void);
uint8_t EPD_Display(unsigned char *image, unsigned char *red_image, unsigned char requested_full_refresh);
void layout_apply_adv_payload(void);
void ble_open_sync_window(void);
extern uint8_t layout_status_buf[16];
extern uint16_t current_battery_mv;
uint16_t battery_read_mv(void);

#define XFER_CTL_BEGIN 0x01u
#define XFER_CTL_ABORT 0x02u
#define XFER_CTL_COMMIT 0x03u
#define XFER_DSL_FORMAT_ZLIB_JSON_V1 0x01u
#define XFER_NO_PROGRESS_TIMEOUT_MS 30000u

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
    uint32_t now_ms;
    uint32_t last_progress_ms;
    uint16_t rendered_uncompressed_len;
} xfer_ctx_t;

static xfer_ctx_t g_xfer;
static uint8_t g_adv_flags;

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

static uint16_t xfer_battery_mv(void)
{
    return current_battery_mv == 0 ? battery_read_mv() : current_battery_mv;
}

uint8_t xfer_adv_flags(void)
{
    return g_adv_flags;
}

static void mark_xfer_error(void)
{
    g_adv_flags |= 0x04u;
    layout_apply_adv_payload();
}

static void clear_xfer_error(void)
{
    g_adv_flags &= (uint8_t)~0x04u;
}

void xfer_init(ack_emit_fn ack)
{
    memset(&g_xfer, 0, sizeof(g_xfer));
    g_xfer.state = XFER_IDLE;
    g_xfer.ack = ack;
    g_adv_flags = 0;
}

void xfer_tick(uint32_t now_ms)
{
    g_xfer.now_ms = now_ms;
    if (g_xfer.state == XFER_RECEIVING &&
        (uint32_t)(now_ms - g_xfer.last_progress_ms) > XFER_NO_PROGRESS_TIMEOUT_MS) {
        ringlog_append(XFER_ERR_BUSY, 0, 0, "watchdog 30s no chunk");
        mark_xfer_error();
        g_xfer.state = XFER_IDLE;
    }
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
    ble_open_sync_window();
    layout_apply_adv_payload();
}

void xfer_handle_ctl(const uint8_t *frame, uint16_t len)
{
    if (frame == NULL || len == 0) {
        return;
    }

    switch (frame[0]) {
    case XFER_CTL_BEGIN:
        if (len < 14 || g_xfer.state != XFER_IDLE) {
            mark_xfer_error();
            emit_ack(0, XFER_ERR_BUSY);
            return;
        }
        g_xfer.data_ver = get_le32(&frame[1]);
        g_xfer.total_len = get_le16(&frame[5]);
        g_xfer.total_chunks = get_le16(&frame[7]);
        g_xfer.crc32 = get_le32(&frame[9]);
        g_xfer.dsl_format = frame[13];
        if (xfer_battery_mv() < XFER_MIN_BATTERY_MV) {
            ringlog_append(XFER_ERR_LOW_BATT, 0, 0, xfer_error_message(XFER_ERR_LOW_BATT));
            mark_xfer_error();
            emit_ack(0, XFER_ERR_LOW_BATT);
            return;
        }
        if (g_xfer.total_len > SLOT_MAX_PAYLOAD || g_xfer.total_chunks > 256 ||
            g_xfer.dsl_format != XFER_DSL_FORMAT_ZLIB_JSON_V1) {
            mark_xfer_error();
            emit_ack(0, XFER_ERR_BUSY);
            return;
        }
        g_xfer.staging = slot_staging();
        slot_erase(g_xfer.staging);
        g_xfer.recv_offset = 0;
        g_xfer.expected_seq = 0;
        g_xfer.rendered_uncompressed_len = 0;
        g_xfer.last_progress_ms = g_xfer.now_ms;
        g_xfer.state = XFER_RECEIVING;
        emit_ack(0, XFER_OK_BEGIN);
        return;

    case XFER_CTL_ABORT:
        g_xfer.state = XFER_IDLE;
        return;

    case XFER_CTL_COMMIT:
        if (len < 5 || g_xfer.state != XFER_RECEIVING || get_le32(&frame[1]) != g_xfer.data_ver ||
            g_xfer.recv_offset != g_xfer.total_len) {
            mark_xfer_error();
            emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_ERR_BUSY);
            return;
        }
        if (verify_staging_crc() != 0) {
            ringlog_append(XFER_ERR_CRC, g_xfer.data_ver, 0, xfer_error_message(XFER_ERR_CRC));
            mark_xfer_error();
            g_xfer.state = XFER_IDLE;
            emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_ERR_CRC);
            return;
        }
        {
            /* Content-addressed dedup: if the active slot already holds this exact
             * data_ver, the panel already shows it. Confirm the commit but skip the
             * redundant full EPD refresh (its ~100-200 mA × ~2 s + ghosting). */
            slot_header_t active;
            if (slot_active(&active) != SLOT_NONE && active.data_ver == g_xfer.data_ver) {
                clear_xfer_error();
                publish_data_ver(g_xfer.data_ver);
                emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_OK_COMMIT);
                g_xfer.state = XFER_IDLE;
                return;
            }
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
        mark_xfer_error();
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
    g_xfer.last_progress_ms = g_xfer.now_ms;
    if ((seq & 0x7u) == 0x7u || g_xfer.recv_offset == g_xfer.total_len) {
        emit_ack(seq, XFER_OK_PROGRESS);
    }
}

static xfer_code_t render_slot_payload(uint32_t addr, uint16_t compressed_len, uint16_t *out_uncompressed_len)
{
    uint8_t compressed[SLOT_MAX_PAYLOAD];
    uint8_t inflate_buf[4096];
    uint8_t arena_buf[4096];
    if (slot_read_payload(addr, compressed, compressed_len) != 0) {
        return XFER_ERR_RENDER;
    }

    epd_clear_buffer();
    pipeline_status_t status = pipeline_run(
        compressed,
        compressed_len,
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

    FixBuffer();
    if (!EPD_Display(epd_buffer, epd_buffer_red, 1)) {
        return XFER_ERR_RENDER;
    }
    change_scene(EPD_SCENE_IMAGE);
    return XFER_OK_COMMIT;
}

static xfer_code_t render_staging(uint16_t *out_uncompressed_len)
{
    return render_slot_payload(slot_addr(g_xfer.staging), g_xfer.total_len, out_uncompressed_len);
}

static void finish_pending_render(void)
{
    slot_header_t active;
    uint32_t next_seq = 1;
    if (slot_active(&active) != SLOT_NONE) {
        next_seq = active.seq + 1;
    }

    slot_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = SLOT_MAGIC_BE;
    hdr.data_ver = g_xfer.data_ver;
    hdr.uncompressed_len = g_xfer.rendered_uncompressed_len;
    hdr.compressed_len = g_xfer.total_len;
    hdr.crc32 = g_xfer.crc32;
    hdr.seq = next_seq;

    if (slot_write_header(g_xfer.staging, &hdr) != 0) {
        ringlog_append(XFER_ERR_RENDER, g_xfer.data_ver, 0, "header write failed");
        mark_xfer_error();
        g_xfer.state = XFER_IDLE;
        emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_ERR_RENDER);
        return;
    }

    clear_xfer_error();
    publish_data_ver(g_xfer.data_ver);
    emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), XFER_OK_COMMIT);
    g_xfer.state = XFER_IDLE;
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
        mark_xfer_error();
        g_xfer.state = XFER_IDLE;
        emit_ack(g_xfer.expected_seq == 0 ? 0 : (uint16_t)(g_xfer.expected_seq - 1), result);
        return;
    }

    g_xfer.rendered_uncompressed_len = uncompressed_len;
    finish_pending_render();
}

static uint8_t g_boot_render_pending;

void xfer_request_boot_render(void)
{
    g_boot_render_pending = 1;
}

void xfer_run_boot_render(void)
{
    if (!g_boot_render_pending) {
        return;
    }
    g_boot_render_pending = 0; /* one-shot, regardless of outcome below */

    if (g_xfer.state != XFER_IDLE) {
        return; /* a live push owns the EPD and will draw the fresh content */
    }

    slot_header_t active;
    slot_id_t id = slot_active(&active);
    if (id == SLOT_NONE) {
        return; /* nothing stored yet — leave whatever the panel retains */
    }
    if (xfer_battery_mv() < XFER_MIN_BATTERY_MV) {
        return; /* don't drive a full refresh on a depleted cell */
    }

    uint16_t uncompressed_len = 0;
    xfer_code_t result = render_slot_payload(slot_addr(id), active.compressed_len, &uncompressed_len);
    if (result != XFER_OK_COMMIT) {
        ringlog_append((uint8_t)result, active.data_ver, 0, xfer_error_message(result));
        mark_xfer_error();
        return;
    }
    publish_data_ver(active.data_ver);
}
