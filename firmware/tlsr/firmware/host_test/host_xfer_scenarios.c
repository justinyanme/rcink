#include "host_runtime.h"

#include "../src/proto/crc32.h"
#include "../src/proto/xfer.h"
#include "../src/store/ringlog.h"
#include "../src/store/slot.h"

#include <stdio.h>
#include <string.h>

extern void host_flash_reset(void);
extern uint8_t host_flash_fail_writes;

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void send_begin_with_format(
    uint32_t data_ver,
    uint16_t total_len,
    uint16_t total_chunks,
    uint32_t crc,
    uint8_t dsl_format
)
{
    uint8_t frame[20] = {0};
    frame[0] = 0x01;
    put_le32(&frame[1], data_ver);
    put_le16(&frame[5], total_len);
    put_le16(&frame[7], total_chunks);
    put_le32(&frame[9], crc);
    frame[13] = dsl_format;
    xfer_handle_ctl(frame, sizeof(frame));
}

static void send_begin(uint32_t data_ver, uint16_t total_len, uint16_t total_chunks, uint32_t crc)
{
    send_begin_with_format(data_ver, total_len, total_chunks, crc, 0x01);
}

static void send_data(uint16_t seq, const uint8_t *payload, uint16_t len)
{
    uint8_t frame[184];
    put_le16(frame, seq);
    memcpy(&frame[2], payload, len);
    xfer_handle_data(frame, (uint16_t)(len + 2));
}

static void send_commit(uint32_t data_ver)
{
    uint8_t frame[20] = {0};
    frame[0] = 0x03;
    put_le32(&frame[1], data_ver);
    xfer_handle_ctl(frame, sizeof(frame));
}

static int expect_ack(uint8_t idx, uint16_t last_seq, uint8_t code)
{
    if (idx >= g_ack_count) {
        fprintf(stderr, "missing ack %u, count=%u\n", idx, g_ack_count);
        return 1;
    }
    if (g_acks[idx].last_seq != last_seq || g_acks[idx].code != code || g_acks[idx].flags != 0) {
        fprintf(stderr,
                "ack %u mismatch: last_seq=%u code=0x%02x flags=0x%02x\n",
                idx,
                g_acks[idx].last_seq,
                g_acks[idx].code,
                g_acks[idx].flags);
        return 1;
    }
    return 0;
}

static int expect_last_ack(uint16_t last_seq, uint8_t code)
{
    if (g_ack_count == 0) {
        fprintf(stderr, "no acks captured\n");
        return 1;
    }
    return expect_ack((uint8_t)(g_ack_count - 1), last_seq, code);
}

static int expect_ringlog(uint8_t err, uint32_t data_ver)
{
    ringlog_record_t rec;
    if (ringlog_count() != 1 || ringlog_read(0, &rec) != 0) {
        fprintf(stderr, "ringlog record missing, count=%u\n", ringlog_count());
        return 1;
    }
    if (rec.err_code != err || rec.data_ver != data_ver) {
        fprintf(stderr, "ringlog mismatch: err=0x%02x data_ver=0x%08x\n", rec.err_code, rec.data_ver);
        return 1;
    }
    return 0;
}

static void send_one_chunk_transfer(uint32_t data_ver, const uint8_t *payload, uint16_t len, uint32_t crc)
{
    send_begin(data_ver, len, 1, crc);
    send_data(0, payload, len);
    send_commit(data_ver);
}

static int write_slot_fixture(slot_id_t slot, const uint8_t *payload, uint16_t len, uint32_t seq, uint32_t crc)
{
    slot_erase(slot);
    if (slot_write_payload(slot, 0, payload, len) != 0) {
        return 1;
    }
    slot_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = SLOT_MAGIC_BE;
    hdr.data_ver = 0xA0000000u | seq;
    hdr.uncompressed_len = len;
    hdr.compressed_len = len;
    hdr.crc32 = crc;
    hdr.seq = seq;
    if (slot_write_header(slot, &hdr) != 0) {
        return 1;
    }
    return 0;
}

static const uint8_t valid_layout_payload[] = {
    0x78, 0xda, 0xab, 0x56, 0x2a, 0xa9, 0x2c, 0x48, 0x55, 0xb2, 0x52, 0x2a,
    0x2b, 0x2e, 0x49, 0x4c, 0xce, 0x56, 0xd2, 0x51, 0x2a, 0x48, 0x4c, 0x51,
    0xb2, 0xb2, 0xd0, 0x51, 0x4a, 0x4f, 0x2c, 0x50, 0xb2, 0x32, 0xd1, 0x51,
    0x4a, 0xce, 0xc8, 0xcc, 0x49, 0x29, 0x4a, 0xcd, 0x53, 0xb2, 0x8a, 0xe6,
    0x52, 0x50, 0xa8, 0x86, 0xa9, 0x2f, 0x49, 0xad, 0x28, 0x01, 0xaa, 0x2e,
    0x4b, 0xcc, 0x29, 0x05, 0x71, 0x3d, 0x5c, 0x7d, 0x7c, 0xfc, 0x81, 0xfc,
    0xe2, 0xcc, 0x2a, 0x10, 0x37, 0xc3, 0x10, 0xc8, 0x4e, 0xce, 0xcf, 0xc9,
    0x2f, 0x02, 0x72, 0x92, 0x72, 0x20, 0x26, 0x27, 0xe6, 0x64, 0xa6, 0x03,
    0x8d, 0x51, 0x4a, 0x4e, 0xcd, 0x2b, 0x49, 0x2d, 0x52, 0xaa, 0xd5, 0xc1,
    0x63, 0x5e, 0x79, 0x7e, 0x51, 0x4e, 0x0a, 0xc2, 0xbc, 0xa4, 0xfc, 0x94,
    0x4a, 0x22, 0x4c, 0xe4, 0x8a, 0xad, 0xe5, 0x02, 0x00, 0x62, 0xda, 0x40,
    0x8a,
};

int xfer_test(const char *scenario)
{
    host_xfer_reset();

    if (strcmp(scenario, "gap") == 0) {
        const uint8_t a[] = {1, 2};
        const uint8_t b[] = {3, 4};
        send_begin(0xAABBCCDDu, 6, 3, 0);
        send_data(0, a, sizeof(a));
        send_data(2, b, sizeof(b));
        if (g_ack_count != 2 || expect_ack(0, 0, XFER_OK_BEGIN) != 0 ||
            expect_ack(1, 0, XFER_ERR_GAP) != 0 || xfer_state() != XFER_RECEIVING) {
            fprintf(stderr, "gap scenario failed\n");
            return 1;
        }
        puts("OK gap");
        return 0;
    }

    if (strcmp(scenario, "too-big") == 0) {
        send_begin(0xC0DEC011u, SLOT_MAX_PAYLOAD + 1, 1, 0);
        if (g_ack_count != 1 || expect_ack(0, 0, XFER_ERR_TOO_BIG) != 0 ||
            expect_ringlog(XFER_ERR_TOO_BIG, 0xC0DEC011u) != 0 || xfer_state() != XFER_IDLE) {
            fprintf(stderr, "too-big scenario failed\n");
            return 1;
        }
        puts("OK too-big");
        return 0;
    }

    if (strcmp(scenario, "format-bad") == 0) {
        send_begin_with_format(0xC0DEC012u, 16, 1, 0, 0xFF);
        if (g_ack_count != 1 || expect_ack(0, 0, XFER_ERR_FORMAT) != 0 ||
            expect_ringlog(XFER_ERR_FORMAT, 0xC0DEC012u) != 0 || xfer_state() != XFER_IDLE) {
            fprintf(stderr, "format-bad scenario failed\n");
            return 1;
        }
        puts("OK format-bad");
        return 0;
    }

    if (strcmp(scenario, "overrun") == 0) {
        const uint8_t payload[] = {1, 2, 3};
        send_begin(0xC0DEC013u, 2, 1, 0);
        send_data(0, payload, sizeof(payload));
        if (g_ack_count != 2 || expect_ack(0, 0, XFER_OK_BEGIN) != 0 ||
            expect_ack(1, 0, XFER_ERR_OVERRUN) != 0 ||
            expect_ringlog(XFER_ERR_OVERRUN, 0xC0DEC013u) != 0 || xfer_state() != XFER_IDLE) {
            fprintf(stderr, "overrun scenario failed\n");
            return 1;
        }
        puts("OK overrun");
        return 0;
    }

    if (strcmp(scenario, "flash-fail") == 0) {
        const uint8_t payload[] = {1, 2};
        host_flash_fail_writes = 1;
        send_begin(0xC0DEC014u, sizeof(payload), 1, 0);
        send_data(0, payload, sizeof(payload));
        if (g_ack_count != 2 || expect_ack(0, 0, XFER_OK_BEGIN) != 0 ||
            expect_ack(1, 0, XFER_ERR_FLASH) != 0 ||
            expect_ringlog(XFER_ERR_FLASH, 0xC0DEC014u) != 0 || xfer_state() != XFER_IDLE) {
            fprintf(stderr, "flash-fail scenario failed\n");
            return 1;
        }
        puts("OK flash-fail");
        return 0;
    }

    if (strcmp(scenario, "commit-invalid-idles") == 0) {
        send_begin(0xC0DEC015u, 4, 1, 0);
        send_commit(0xC0DEC015u);
        if (g_ack_count != 2 || expect_ack(0, 0, XFER_OK_BEGIN) != 0 ||
            expect_ack(1, 0, XFER_ERR_BUSY) != 0 || xfer_state() != XFER_IDLE) {
            fprintf(stderr, "commit-invalid-idles scenario failed\n");
            return 1;
        }
        puts("OK commit-invalid-idles");
        return 0;
    }

    if (strcmp(scenario, "crc-bad") == 0) {
        static const uint8_t payload[] = {0x78, 0x9c, 0xcb, 0xcb, 0x2f, 0x51, 0xc8, 0x2a,
                                          0xce, 0xcf, 0x03, 0x00, 0x0e, 0x26, 0x03, 0x2c};
        send_one_chunk_transfer(0xC0DEC001u, payload, sizeof(payload), 0xDEADBEEFu);
        if (expect_last_ack(0, XFER_ERR_CRC) != 0 || expect_ringlog(XFER_ERR_CRC, 0xC0DEC001u) != 0 ||
            xfer_state() != XFER_IDLE) {
            fprintf(stderr, "crc-bad scenario failed\n");
            return 1;
        }
        puts("OK crc-bad");
        return 0;
    }

    if (strcmp(scenario, "inflate-bad") == 0) {
        static const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04, 0x05};
        send_one_chunk_transfer(0xC0DEC002u, payload, sizeof(payload), crc32_zlib(payload, sizeof(payload)));
        xfer_run_pending_render();
        if (expect_last_ack(0, XFER_ERR_INFLATE) != 0 ||
            expect_ringlog(XFER_ERR_INFLATE, 0xC0DEC002u) != 0 || xfer_state() != XFER_IDLE) {
            fprintf(stderr, "inflate-bad scenario failed\n");
            return 1;
        }
        puts("OK inflate-bad");
        return 0;
    }

    if (strcmp(scenario, "parse-bad") == 0) {
        static const uint8_t payload[] = {0x78, 0x9c, 0xcb, 0xcb, 0x2f, 0x51, 0xc8, 0x2a,
                                          0xce, 0xcf, 0x03, 0x00, 0x0e, 0x26, 0x03, 0x2c};
        send_one_chunk_transfer(0xC0DEC003u, payload, sizeof(payload), crc32_zlib(payload, sizeof(payload)));
        xfer_run_pending_render();
        if (expect_last_ack(0, XFER_ERR_PARSE) != 0 || expect_ringlog(XFER_ERR_PARSE, 0xC0DEC003u) != 0 ||
            xfer_state() != XFER_IDLE) {
            fprintf(stderr, "parse-bad scenario failed\n");
            return 1;
        }
        puts("OK parse-bad");
        return 0;
    }

    if (strcmp(scenario, "render-fail") == 0) {
        g_host_epd_display_ok = 0;
        send_one_chunk_transfer(
            0xC0DEC006u,
            valid_layout_payload,
            sizeof(valid_layout_payload),
            crc32_zlib(valid_layout_payload, sizeof(valid_layout_payload))
        );
        xfer_run_pending_render();
        if (expect_last_ack(0, XFER_ERR_RENDER) != 0 ||
            expect_ringlog(XFER_ERR_RENDER, 0xC0DEC006u) != 0 || xfer_state() != XFER_IDLE ||
            (xfer_adv_flags() & 0x04u) == 0) {
            fprintf(stderr, "render-fail scenario failed\n");
            return 1;
        }
        puts("OK render-fail");
        return 0;
    }

    if (strcmp(scenario, "commit-success-image-scene") == 0) {
        const uint32_t data_ver = 0xC0DEC008u;
        send_one_chunk_transfer(
            data_ver,
            valid_layout_payload,
            sizeof(valid_layout_payload),
            crc32_zlib(valid_layout_payload, sizeof(valid_layout_payload))
        );
        xfer_run_pending_render();
        if (expect_last_ack(0, XFER_OK_COMMIT) != 0 || xfer_state() != XFER_IDLE ||
            g_host_epd_display_count != 1 || g_host_fix_buffer_count != 1 ||
            g_host_scene != EPD_SCENE_IMAGE ||
            layout_status_buf[0] != (uint8_t)(data_ver & 0xFF) ||
            layout_status_buf[1] != (uint8_t)((data_ver >> 8) & 0xFF) ||
            layout_status_buf[2] != (uint8_t)((data_ver >> 16) & 0xFF) ||
            layout_status_buf[3] != (uint8_t)(data_ver >> 24)) {
            fprintf(stderr, "commit-success-image-scene scenario failed\n");
            return 1;
        }
        puts("OK commit-success-image-scene");
        return 0;
    }

    if (strcmp(scenario, "busy-display-commits") == 0) {
        g_host_epd_display_busy = 1;
        send_one_chunk_transfer(
            0xC0DEC007u,
            valid_layout_payload,
            sizeof(valid_layout_payload),
            crc32_zlib(valid_layout_payload, sizeof(valid_layout_payload))
        );
        xfer_run_pending_render();
        if (expect_last_ack(0, XFER_OK_COMMIT) != 0 || xfer_state() != XFER_IDLE ||
            g_host_epd_display_count != 1 || g_host_fix_buffer_count != 1 ||
            g_host_scene != EPD_SCENE_IMAGE) {
            fprintf(stderr, "busy-display-commits scenario failed\n");
            return 1;
        }
        puts("OK busy-display-commits");
        return 0;
    }

    if (strcmp(scenario, "low-batt") == 0) {
        g_host_battery_mv = 2300;
        send_begin(0xC0DEC004u, 16, 1, 0);
        if (g_ack_count != 1 || expect_ack(0, 0, XFER_ERR_LOW_BATT) != 0 ||
            expect_ringlog(XFER_ERR_LOW_BATT, 0) != 0 || xfer_state() != XFER_IDLE ||
            (xfer_adv_flags() & 0x04u) == 0) {
            fprintf(stderr, "low-batt scenario failed\n");
            return 1;
        }
        puts("OK low-batt");
        return 0;
    }

    if (strcmp(scenario, "cached-batt") == 0) {
        current_battery_mv = 3200;
        g_host_battery_mv = 1;
        send_begin(0xC0DEC009u, sizeof(valid_layout_payload), 1, crc32_zlib(valid_layout_payload, sizeof(valid_layout_payload)));
        if (g_ack_count != 1 || expect_ack(0, 0, XFER_OK_BEGIN) != 0 || xfer_state() != XFER_RECEIVING) {
            fprintf(stderr, "cached-batt scenario failed\n");
            return 1;
        }
        puts("OK cached-batt");
        return 0;
    }

    if (strcmp(scenario, "watchdog") == 0) {
        const uint8_t a[] = {1, 2};
        send_begin(0xC0DEC005u, 4, 2, 0);
        send_data(0, a, sizeof(a));
        g_host_now_ms = 35000;
        xfer_tick(g_host_now_ms);
        if (xfer_state() != XFER_IDLE || g_ack_count != 1 || expect_ack(0, 0, XFER_OK_BEGIN) != 0 ||
            expect_ringlog(XFER_ERR_BUSY, 0) != 0) {
            fprintf(stderr, "watchdog scenario failed\n");
            return 1;
        }
        puts("OK watchdog");
        return 0;
    }

    if (strcmp(scenario, "commit-skip-duplicate") == 0) {
        const uint32_t dv = 0xC0DEC00Au;
        const uint16_t len = sizeof(valid_layout_payload);
        const uint32_t crc = crc32_zlib(valid_layout_payload, len);

        send_one_chunk_transfer(dv, valid_layout_payload, len, crc);
        xfer_run_pending_render();
        if (expect_last_ack(0, XFER_OK_COMMIT) != 0 || g_host_epd_display_count != 1) {
            fprintf(stderr, "commit-skip-duplicate: first push failed (count=%u)\n", g_host_epd_display_count);
            return 1;
        }

        send_one_chunk_transfer(dv, valid_layout_payload, len, crc);
        xfer_run_pending_render();
        if (expect_last_ack(0, XFER_OK_COMMIT) != 0 || xfer_state() != XFER_IDLE ||
            g_host_epd_display_count != 1) {
            fprintf(stderr, "commit-skip-duplicate: second push re-rendered (count=%u)\n", g_host_epd_display_count);
            return 1;
        }
        puts("OK commit-skip-duplicate");
        return 0;
    }

    if (strcmp(scenario, "boot-redraw") == 0) {
        const uint16_t len = sizeof(valid_layout_payload);
        const uint32_t dv = 0xA0000001u;
        if (write_slot_fixture(SLOT_A, valid_layout_payload, len, 1, crc32_zlib(valid_layout_payload, len)) != 0) {
            return 1;
        }
        xfer_request_boot_render();
        xfer_run_boot_render();
        if (g_host_epd_display_count != 1 || g_host_fix_buffer_count != 1 ||
            g_host_scene != EPD_SCENE_IMAGE ||
            layout_status_buf[0] != (uint8_t)(dv & 0xFF) ||
            layout_status_buf[3] != (uint8_t)(dv >> 24)) {
            fprintf(stderr, "boot-redraw failed (count=%u scene=%d)\n", g_host_epd_display_count, g_host_scene);
            return 1;
        }
        xfer_run_boot_render();
        if (g_host_epd_display_count != 1) {
            fprintf(stderr, "boot-redraw not one-shot (count=%u)\n", g_host_epd_display_count);
            return 1;
        }
        puts("OK boot-redraw");
        return 0;
    }

    if (strcmp(scenario, "boot-redraw-empty") == 0) {
        host_flash_reset();
        xfer_request_boot_render();
        xfer_run_boot_render();
        if (g_host_epd_display_count != 0) {
            fprintf(stderr, "boot-redraw-empty drew with no slot (count=%u)\n", g_host_epd_display_count);
            return 1;
        }
        puts("OK boot-redraw-empty");
        return 0;
    }

    fprintf(stderr, "unknown xfer-test scenario: %s\n", scenario);
    return 1;
}
