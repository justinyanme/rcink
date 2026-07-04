#include "host_canvas.h"
#include "host_runtime.h"

#include "../src/dsl/arena.h"
#include "../src/dsl/parser.h"
#include "../src/engine/engine.h"
#include "../src/epd/scene.h"
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
int slot_test(const char *scenario);
int ringlog_test(const char *scenario);
int xfer_test(const char *scenario);
int text_dy_test(void);

#define HOST_EPD_BUFFER_SIZE (300 * 128 / 8)

uint8_t epd_buffer[HOST_EPD_BUFFER_SIZE];
uint8_t epd_buffer_red[HOST_EPD_BUFFER_SIZE];
uint8_t layout_status_buf[16];
uint16_t current_battery_mv;

uint32_t g_host_now_ms;
uint16_t g_host_battery_mv = 3000;
uint8_t g_host_epd_display_ok = 1;
uint8_t g_host_epd_display_busy = 0;
uint8_t g_host_epd_display_count = 0;
uint8_t g_host_fix_buffer_count = 0;
EPD_SCENE g_host_scene = EPD_SCENE_DEFAULT;
host_ack_t g_acks[16];
uint8_t g_ack_count;

uint32_t host_now_ms(void)
{
    return g_host_now_ms;
}

uint16_t battery_read_mv(void)
{
    return g_host_battery_mv;
}

void epd_clear_buffer(void)
{
    memset(epd_buffer, 0xFF, sizeof(epd_buffer));
    memset(epd_buffer_red, 0x00, sizeof(epd_buffer_red));
}

void FixBuffer(void)
{
    g_host_fix_buffer_count++;
}

uint8_t EPD_Display(unsigned char *image, unsigned char *red_image, unsigned char requested_full_refresh)
{
    (void)image;
    (void)red_image;
    (void)requested_full_refresh;
    g_host_epd_display_count++;
    return g_host_epd_display_ok;
}

uint8_t epd_is_display_busy(void)
{
    return g_host_epd_display_busy;
}

void change_scene(EPD_SCENE scene)
{
    g_host_scene = scene;
}

void layout_refresh_adv_payload(void)
{
}

void layout_apply_adv_payload(void)
{
}

void ble_open_sync_window(void)
{
}

static void host_ack_emit(uint16_t last_seq, uint8_t code, uint8_t flags)
{
    if (g_ack_count >= sizeof(g_acks) / sizeof(g_acks[0])) {
        return;
    }
    g_acks[g_ack_count++] = (host_ack_t){last_seq, code, flags};
}

void host_xfer_reset(void)
{
    host_flash_reset();
    memset(layout_status_buf, 0, sizeof(layout_status_buf));
    memset(g_acks, 0, sizeof(g_acks));
    g_ack_count = 0;
    g_host_now_ms = 0;
    g_host_battery_mv = 3000;
    current_battery_mv = 0;
    g_host_epd_display_ok = 1;
    g_host_epd_display_busy = 0;
    g_host_epd_display_count = 0;
    g_host_fix_buffer_count = 0;
    g_host_scene = EPD_SCENE_TIME;
    ringlog_init();
    xfer_init(host_ack_emit);
    xfer_tick(g_host_now_ms);
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

static int float_pct_test(void)
{
    /* Regression: JSON floats (e.g. "pct":50.0, sparkline number[]) must parse,
     * not silently fall back to 0. Renders a half-full bar and checks the fill. */
    static uint8_t parse_buf[8192];
    static uint8_t layout_buf[8192];
    arena_t pa;
    arena_t la;
    arena_init(&pa, parse_buf, sizeof(parse_buf));
    arena_init(&la, layout_buf, sizeof(layout_buf));

    const char *json =
        "{\"type\":\"vstack\",\"pad\":2,\"children\":"
        "[{\"type\":\"hbar\",\"pct\":50.0,\"color\":\"black\",\"height\":60}]}";
    parse_result_t pr;
    layout_node_t *root = parse_layout(json, strlen(json), &pa, &pr);
    if (root == NULL) {
        fprintf(stderr, "float-pct: parse failed (%d)\n", pr.status);
        return 1;
    }
    laid_out_node_t *laid = run_layout(root, (rect_t){0, 0, 250, 128}, &host_measurer, &la);
    if (laid == NULL) {
        fprintf(stderr, "float-pct: layout failed\n");
        return 1;
    }
    host_canvas_init(250, 128);
    host_canvas_clear(1);
    paint_layout(laid);

    int16_t w = 0;
    int16_t h = 0;
    const uint8_t *buf = host_canvas_buffer(&w, &h);
    const int y = 30; /* inside the 60px bar */
    int left = 0;
    int right = 0;
    for (int x = 0; x < w / 2; x++) {
        if (buf[y * w + x] == 0) {
            left++;
        }
    }
    for (int x = w / 2; x < w; x++) {
        if (buf[y * w + x] == 0) {
            right++;
        }
    }
    /* pct=50 → left half substantially filled, right half ~empty (borders only). */
    if (left < w / 4 || right > w / 8) {
        fprintf(stderr, "float-pct fill wrong: left=%d right=%d (w=%d)\n", left, right, w);
        return 1;
    }
    puts("OK float-pct");
    return 0;
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

static int wire_constants(void)
{
    printf("{");
    printf("\"ctl\":{");
    printf("\"begin\":%u,\"abort\":%u,\"commit\":%u",
           (unsigned)XFER_CTL_BEGIN,
           (unsigned)XFER_CTL_ABORT,
           (unsigned)XFER_CTL_COMMIT);
    printf("},");
    printf("\"frame\":{");
    printf("\"len\":%u,", (unsigned)XFER_FRAME_LEN);
    printf("\"ctl_off_cmd\":%u,", (unsigned)XFER_CTL_OFF_CMD);
    printf("\"begin_min_len\":%u,", (unsigned)XFER_BEGIN_MIN_LEN);
    printf("\"begin_off_data_ver\":%u,", (unsigned)XFER_BEGIN_OFF_DATA_VER);
    printf("\"begin_off_total_len\":%u,", (unsigned)XFER_BEGIN_OFF_TOTAL_LEN);
    printf("\"begin_off_total_chunks\":%u,", (unsigned)XFER_BEGIN_OFF_TOTAL_CHUNKS);
    printf("\"begin_off_crc32\":%u,", (unsigned)XFER_BEGIN_OFF_CRC32);
    printf("\"begin_off_dsl_format\":%u,", (unsigned)XFER_BEGIN_OFF_DSL_FORMAT);
    printf("\"begin_off_reserved\":%u,", (unsigned)XFER_BEGIN_OFF_RESERVED);
    printf("\"commit_min_len\":%u,", (unsigned)XFER_COMMIT_MIN_LEN);
    printf("\"commit_off_data_ver\":%u,", (unsigned)XFER_COMMIT_OFF_DATA_VER);
    printf("\"data_off_seq\":%u,", (unsigned)XFER_DATA_OFF_SEQ);
    printf("\"data_off_payload\":%u,", (unsigned)XFER_DATA_OFF_PAYLOAD);
    printf("\"dsl_format_zlib_json_v1\":%u", (unsigned)XFER_DSL_FORMAT_ZLIB_JSON_V1);
    printf("},");
    printf("\"ack\":{");
    printf("\"ok_progress\":%u,\"ok_begin\":%u,\"ok_commit\":%u,",
           (unsigned)XFER_OK_PROGRESS,
           (unsigned)XFER_OK_BEGIN,
           (unsigned)XFER_OK_COMMIT);
    printf("\"err_gap\":%u,\"err_crc\":%u,\"err_inflate\":%u,\"err_parse\":%u,",
           (unsigned)XFER_ERR_GAP,
           (unsigned)XFER_ERR_CRC,
           (unsigned)XFER_ERR_INFLATE,
           (unsigned)XFER_ERR_PARSE);
    printf("\"err_render\":%u,\"err_too_big\":%u,\"err_format\":%u,\"err_overrun\":%u,",
           (unsigned)XFER_ERR_RENDER,
           (unsigned)XFER_ERR_TOO_BIG,
           (unsigned)XFER_ERR_FORMAT,
           (unsigned)XFER_ERR_OVERRUN);
    printf("\"err_flash\":%u,\"err_low_batt\":%u,\"err_busy\":%u",
           (unsigned)XFER_ERR_FLASH,
           (unsigned)XFER_ERR_LOW_BATT,
           (unsigned)XFER_ERR_BUSY);
    printf("},");
    printf("\"adv\":{");
    printf("\"payload_type_v2\":%u,", (unsigned)ADV_PAYLOAD_TYPE_V2);
    printf("\"sync_open\":%u,", (unsigned)ADV_FLAG_SYNC_OPEN);
    printf("\"low_batt\":%u,", (unsigned)ADV_FLAG_LOW_BATT);
    printf("\"xfer_err\":%u,", (unsigned)ADV_FLAG_XFER_ERR);
    printf("\"display_busy\":%u", (unsigned)ADV_FLAG_DISPLAY_BUSY);
    printf("},");
    printf("\"limits\":{");
    printf("\"min_battery_mv\":%u,", (unsigned)XFER_MIN_BATTERY_MV);
    printf("\"slot_header_size\":%u,", (unsigned)SLOT_HEADER_SIZE);
    printf("\"slot_max_payload\":%u", (unsigned)SLOT_MAX_PAYLOAD);
    printf("}");
    printf("}\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "version") == 0) {
        puts("tlsr-layout-host-test phase3");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "wire-constants") == 0) {
        return wire_constants();
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
    if (argc >= 2 && strcmp(argv[1], "float-pct-test") == 0) {
        return float_pct_test();
    }
    if (argc >= 2 && strcmp(argv[1], "text-dy-test") == 0) {
        return text_dy_test();
    }
    if (argc < 3) {
        fprintf(stderr, "usage: host_test layout <scenario.json> | render <scenario.json> --pgm out.pgm [--png out.png] | wire-constants | uzlib-test <in.zlib> | slot-test <scenario> | ringlog-test <scenario> | xfer-test <scenario> | float-pct-test | text-dy-test | pipeline-e2e --uncompressed-len n --png out.png\n");
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
