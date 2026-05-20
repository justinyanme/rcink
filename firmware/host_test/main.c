#include "host_canvas.h"

#include "../src/dsl/arena.h"
#include "../src/dsl/parser.h"
#include "../src/engine/engine.h"
#include "../src/proto/uzlib/uzlib.h"
#include "../src/render/paint.h"
#include "../src/render/pipeline.h"
#include "../src/proto/crc32.h"
#include "../src/proto/xfer.h"
#include "../src/store/ringlog.h"
#include "../src/store/slot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const measurer_t host_measurer;
extern void host_flash_reset(void);

#define HOST_EPD_BUFFER_SIZE (300 * 128 / 8)

uint8_t epd_buffer[HOST_EPD_BUFFER_SIZE];
uint8_t epd_buffer_red[HOST_EPD_BUFFER_SIZE];
uint8_t layout_status_buf[16];

static uint32_t g_host_now_ms;

typedef struct {
    uint16_t last_seq;
    uint8_t code;
    uint8_t flags;
} host_ack_t;

static host_ack_t g_acks[16];
static uint8_t g_ack_count;

uint32_t host_now_ms(void)
{
    return g_host_now_ms;
}

void epd_clear_buffer(void)
{
    memset(epd_buffer, 0xFF, sizeof(epd_buffer));
    memset(epd_buffer_red, 0x00, sizeof(epd_buffer_red));
}

void EPD_Display(unsigned char *image, unsigned char *red_image, unsigned char requested_full_refresh)
{
    (void)image;
    (void)red_image;
    (void)requested_full_refresh;
}

void layout_refresh_adv_payload(void)
{
}

static void host_ack_emit(uint16_t last_seq, uint8_t code, uint8_t flags)
{
    if (g_ack_count >= sizeof(g_acks) / sizeof(g_acks[0])) {
        return;
    }
    g_acks[g_ack_count++] = (host_ack_t){last_seq, code, flags};
}

static void host_xfer_reset(void)
{
    host_flash_reset();
    memset(layout_status_buf, 0, sizeof(layout_status_buf));
    memset(g_acks, 0, sizeof(g_acks));
    g_ack_count = 0;
    g_host_now_ms = 0;
    ringlog_init();
    xfer_init(host_ack_emit);
}

static const char *kind_name(layout_kind_t kind)
{
    switch (kind) {
    case LN_VSTACK: return "vstack";
    case LN_HSTACK: return "hstack";
    case LN_SPACER: return "spacer";
    case LN_DIVIDER: return "divider";
    case LN_TEXT: return "text";
    case LN_HBAR: return "hbar";
    case LN_ICON: return "icon";
    case LN_IMAGE: return "image";
    case LN_SPARKLINE: return "sparkline";
    case LN_MARKER: return "marker";
    }
    return "unknown";
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    *len = (size_t)n;
    return buf;
}

static void emit_layout_node(const laid_out_node_t *n, int *first)
{
    if (!*first) {
        printf(",");
    }
    *first = 0;
    printf("{\"kind\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",
           kind_name(n->node->kind), n->rect.x, n->rect.y, n->rect.w, n->rect.h);
    for (uint8_t i = 0; i < n->child_count; i++) {
        emit_layout_node(&n->children[i], first);
    }
}

static int load_and_layout(const char *path, arena_t *parse_arena, arena_t *layout_arena, laid_out_node_t **out)
{
    size_t json_len = 0;
    char *json = read_file(path, &json_len);
    if (json == NULL) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }
    parse_result_t result;
    layout_node_t *root = parse_layout(json, json_len, parse_arena, &result);
    free(json);
    if (root == NULL) {
        fprintf(stderr, "parse error %d at %d: %s\n", result.status, result.offset, result.message);
        return 1;
    }
    *out = run_layout(root, (rect_t){0, 0, 250, 128}, &host_measurer, layout_arena);
    if (*out == NULL) {
        fprintf(stderr, "layout failed\n");
        return 1;
    }
    return 0;
}

static const char *arg_value(int argc, char **argv, const char *name)
{
    for (int i = 2; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static int uzlib_test(const char *path)
{
    size_t compressed_len = 0;
    char *compressed = read_file(path, &compressed_len);
    if (compressed == NULL) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }

    uint8_t *out = malloc(65536);
    if (out == NULL) {
        free(compressed);
        fprintf(stderr, "failed to allocate inflate buffer\n");
        return 1;
    }

    struct uzlib_uncomp ctx;
    memset(&ctx, 0, sizeof(ctx));
    uzlib_init();
    uzlib_uncompress_init(&ctx, NULL, 0);
    ctx.source = (const unsigned char *)compressed;
    ctx.source_limit = (const unsigned char *)compressed + compressed_len;
    ctx.source_read_cb = NULL;
    ctx.dest_start = out;
    ctx.dest = out;
    ctx.dest_limit = out + 65536;

    if (uzlib_zlib_parse_header(&ctx) < 0) {
        free(out);
        free(compressed);
        fprintf(stderr, "zlib header parse failed\n");
        return 1;
    }
    int rc = uzlib_uncompress_chksum(&ctx);
    if (rc != TINF_DONE) {
        free(out);
        free(compressed);
        fprintf(stderr, "inflate failed: %d\n", rc);
        return 1;
    }

    const size_t out_len = (size_t)(ctx.dest - ctx.dest_start);
    if (fwrite(out, 1, out_len, stdout) != out_len) {
        free(out);
        free(compressed);
        fprintf(stderr, "stdout write failed\n");
        return 1;
    }
    free(out);
    free(compressed);
    return 0;
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

static int slot_test(const char *scenario)
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

static int ringlog_test(const char *scenario)
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

static void send_begin(uint32_t data_ver, uint16_t total_len, uint16_t total_chunks, uint32_t crc)
{
    uint8_t frame[20] = {0};
    frame[0] = 0x01;
    put_le32(&frame[1], data_ver);
    put_le16(&frame[5], total_len);
    put_le16(&frame[7], total_chunks);
    put_le32(&frame[9], crc);
    frame[13] = 0x01;
    xfer_handle_ctl(frame, sizeof(frame));
}

static void send_data(uint16_t seq, const uint8_t *payload, uint16_t len)
{
    uint8_t frame[184];
    put_le16(frame, seq);
    memcpy(&frame[2], payload, len);
    xfer_handle_data(frame, (uint16_t)(len + 2));
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

static int xfer_test(const char *scenario)
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

    fprintf(stderr, "unknown xfer-test scenario: %s\n", scenario);
    return 1;
}

static uint8_t *read_stdin(size_t *len)
{
    size_t cap = 4096;
    size_t used = 0;
    uint8_t *buf = malloc(cap);
    if (buf == NULL) {
        return NULL;
    }
    for (;;) {
        if (used == cap) {
            size_t next_cap = cap * 2;
            uint8_t *next = realloc(buf, next_cap);
            if (next == NULL) {
                free(buf);
                return NULL;
            }
            buf = next;
            cap = next_cap;
        }
        size_t got = fread(buf + used, 1, cap - used, stdin);
        used += got;
        if (got == 0) {
            if (ferror(stdin)) {
                free(buf);
                return NULL;
            }
            break;
        }
    }
    *len = used;
    return buf;
}

static int pipeline_e2e(int argc, char **argv)
{
    const char *len_s = arg_value(argc, argv, "--uncompressed-len");
    const char *png = arg_value(argc, argv, "--png");
    if (len_s == NULL || png == NULL) {
        fprintf(stderr, "pipeline-e2e requires --uncompressed-len and --png\n");
        return 1;
    }

    unsigned long uncompressed_len = strtoul(len_s, NULL, 10);
    if (uncompressed_len == 0 || uncompressed_len > 4096) {
        fprintf(stderr, "invalid uncompressed length: %lu\n", uncompressed_len);
        return 1;
    }

    size_t compressed_len = 0;
    uint8_t *compressed = read_stdin(&compressed_len);
    if (compressed == NULL || compressed_len > 65535) {
        free(compressed);
        fprintf(stderr, "failed to read compressed stdin\n");
        return 1;
    }

    uint8_t inflate_buf[4096];
    uint8_t arena_buf[4096];
    uint16_t actual_len = 0;
    host_canvas_init(250, 128);
    host_canvas_clear(1);
    pipeline_status_t status = pipeline_run(
        compressed,
        (uint16_t)compressed_len,
        sizeof(inflate_buf),
        inflate_buf,
        arena_buf,
        &actual_len
    );
    free(compressed);
    if (status != PIPE_OK || actual_len != (uint16_t)uncompressed_len) {
        fprintf(stderr, "pipeline failed: status=%d actual_len=%u\n", status, actual_len);
        return 1;
    }
    if (host_canvas_write_png(png) != 0) {
        fprintf(stderr, "failed to write %s\n", png);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "version") == 0) {
        puts("tlsr-layout-host-test phase3");
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "uzlib-test") == 0) {
        return uzlib_test(argv[2]);
    }
    if (argc >= 3 && strcmp(argv[1], "slot-test") == 0) {
        return slot_test(argv[2]);
    }
    if (argc >= 3 && strcmp(argv[1], "ringlog-test") == 0) {
        return ringlog_test(argv[2]);
    }
    if (argc >= 3 && strcmp(argv[1], "xfer-test") == 0) {
        return xfer_test(argv[2]);
    }
    if (argc >= 2 && strcmp(argv[1], "pipeline-e2e") == 0) {
        return pipeline_e2e(argc, argv);
    }
    if (argc < 3) {
        fprintf(stderr, "usage: host_test layout <scenario.json> | render <scenario.json> --pgm out.pgm [--png out.png] | uzlib-test <in.zlib> | slot-test <scenario> | ringlog-test <scenario> | xfer-test <scenario> | pipeline-e2e --uncompressed-len n --png out.png\n");
        return 1;
    }

    uint8_t parse_buf[65536];
    uint8_t layout_buf[65536];
    arena_t parse_arena;
    arena_t layout_arena;
    arena_init(&parse_arena, parse_buf, sizeof(parse_buf));
    arena_init(&layout_arena, layout_buf, sizeof(layout_buf));

    laid_out_node_t *laid = NULL;
    if (load_and_layout(argv[2], &parse_arena, &layout_arena, &laid) != 0) {
        return 1;
    }

    if (strcmp(argv[1], "layout") == 0) {
        int first = 1;
        printf("[");
        emit_layout_node(laid, &first);
        printf("]\n");
        return 0;
    }
    if (strcmp(argv[1], "render") == 0) {
        const char *pgm = arg_value(argc, argv, "--pgm");
        const char *png = arg_value(argc, argv, "--png");
        if (pgm == NULL) {
            fprintf(stderr, "render requires --pgm\n");
            return 1;
        }
        host_canvas_init(250, 128);
        host_canvas_clear(1);
        paint_layout(laid);
        if (host_canvas_write_pgm(pgm) != 0) {
            fprintf(stderr, "failed to write %s\n", pgm);
            return 1;
        }
        if (png != NULL && host_canvas_write_png(png) != 0) {
            fprintf(stderr, "failed to write %s\n", png);
            return 1;
        }
        return 0;
    }
    fprintf(stderr, "unknown command: %s\n", argv[1]);
    return 1;
}
