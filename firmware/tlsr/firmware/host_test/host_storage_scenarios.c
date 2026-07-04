#include "host_runtime.h"

#include "../src/proto/crc32.h"
#include "../src/store/ringlog.h"
#include "../src/store/slot.h"

#include <stdio.h>
#include <string.h>

extern void host_flash_reset(void);

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

int slot_test(const char *scenario)
{
    static const uint8_t payload_a[] = {1, 2, 3, 4, 5};
    static const uint8_t payload_b[] = {9, 8, 7, 6};
    host_flash_reset();

    if (strcmp(scenario, "roundtrip") == 0) {
        const uint32_t crc = crc32_zlib(payload_a, sizeof(payload_a));
        if (write_slot_fixture(SLOT_A, payload_a, sizeof(payload_a), 1, crc) != 0) {
            return 1;
        }
        slot_header_t hdr;
        uint8_t out[sizeof(payload_a)];
        if (slot_active(&hdr) != SLOT_A || hdr.seq != 1 ||
            slot_read_payload(SLOT_A_ADDR, out, sizeof(out)) != 0 ||
            memcmp(out, payload_a, sizeof(out)) != 0) {
            fprintf(stderr, "roundtrip failed\n");
            return 1;
        }
        puts("OK roundtrip");
        return 0;
    }

    if (strcmp(scenario, "highest-seq") == 0) {
        if (write_slot_fixture(SLOT_A, payload_a, sizeof(payload_a), 1, crc32_zlib(payload_a, sizeof(payload_a))) != 0 ||
            write_slot_fixture(SLOT_B, payload_b, sizeof(payload_b), 2, crc32_zlib(payload_b, sizeof(payload_b))) != 0) {
            return 1;
        }
        slot_header_t hdr;
        if (slot_active(&hdr) != SLOT_B || hdr.seq != 2) {
            fprintf(stderr, "highest-seq failed\n");
            return 1;
        }
        puts("OK highest-seq");
        return 0;
    }

    if (strcmp(scenario, "invalid-crc") == 0) {
        if (write_slot_fixture(SLOT_A, payload_a, sizeof(payload_a), 1, crc32_zlib(payload_a, sizeof(payload_a))) != 0 ||
            write_slot_fixture(SLOT_B, payload_b, sizeof(payload_b), 2, 0xDEADBEEFu) != 0) {
            return 1;
        }
        slot_header_t hdr;
        if (slot_active(&hdr) != SLOT_A || hdr.seq != 1) {
            fprintf(stderr, "invalid-crc fallback failed\n");
            return 1;
        }
        puts("OK invalid-crc");
        return 0;
    }

    fprintf(stderr, "unknown slot-test scenario: %s\n", scenario);
    return 1;
}

static int assert_ringlog_latest(uint8_t err, uint32_t data_ver, const char *message)
{
    ringlog_record_t rec;
    if (ringlog_read(0, &rec) != 0) {
        fprintf(stderr, "ringlog latest missing\n");
        return 1;
    }
    if (rec.err_code != err || rec.data_ver != data_ver || strcmp((const char *)rec.message, message) != 0) {
        fprintf(stderr,
                "ringlog latest mismatch: err=%u data_ver=0x%08x msg=%s\n",
                rec.err_code,
                rec.data_ver,
                rec.message);
        return 1;
    }
    return 0;
}

int ringlog_test(const char *scenario)
{
    host_flash_reset();
    g_host_now_ms = 1000;
    ringlog_init();

    if (strcmp(scenario, "basic-append") == 0) {
        ringlog_append(0x14, 0x12345678u, 7, "render failed");
        ringlog_record_t rec;
        if (ringlog_count() != 1 || ringlog_read(0, &rec) != 0 ||
            rec.ts != 1000 || rec.err_code != 0x14 || rec.data_ver != 0x12345678u ||
            rec.partial_json_off != 7 || strcmp((const char *)rec.message, "render failed") != 0) {
            fprintf(stderr, "basic append failed\n");
            return 1;
        }
        puts("OK basic-append");
        return 0;
    }

    if (strcmp(scenario, "wraps-after-eight") == 0) {
        for (uint8_t i = 0; i < 8; i++) {
            g_host_now_ms++;
            ringlog_append((uint8_t)(0x10 + i), i, 0, "old");
        }
        g_host_now_ms++;
        ringlog_append(0x21, 9, 0, "new");
        if (ringlog_count() != 1 || assert_ringlog_latest(0x21, 9, "new") != 0) {
            fprintf(stderr, "wrap failed\n");
            return 1;
        }
        puts("OK wraps-after-eight");
        return 0;
    }

    if (strcmp(scenario, "init-finds-existing-tail") == 0) {
        ringlog_append(0x11, 1, 0, "first");
        g_host_now_ms++;
        ringlog_append(0x12, 2, 0, "second");
        ringlog_init();
        ringlog_record_t newest;
        ringlog_record_t older;
        if (ringlog_count() != 2 || ringlog_read(0, &newest) != 0 || ringlog_read(1, &older) != 0 ||
            newest.err_code != 0x12 || newest.data_ver != 2 || older.err_code != 0x11 || older.data_ver != 1) {
            fprintf(stderr, "init tail discovery failed\n");
            return 1;
        }
        puts("OK init-finds-existing-tail");
        return 0;
    }

    fprintf(stderr, "unknown ringlog-test scenario: %s\n", scenario);
    return 1;
}
