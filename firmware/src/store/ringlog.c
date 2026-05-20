#include "ringlog.h"

#include <string.h>

#ifdef TLSR_LAYOUT_HOST_BUILD
extern uint32_t host_now_ms(void);
#else
#include "drivers.h"
extern unsigned int sys_tick_per_us;
#endif

extern void flash_erase_sector(unsigned long addr);
extern void (*flash_read_page)(unsigned long addr, unsigned long len, unsigned char *buf);
extern void (*flash_write_page)(unsigned long addr, unsigned long len, unsigned char *buf);

static uint8_t g_write_idx;
static uint8_t g_count;
static uint8_t g_initialised;

static uint32_t record_addr(uint8_t idx)
{
    return RINGLOG_ADDR + (uint32_t)idx * RINGLOG_RECORD_SIZE;
}

static uint32_t now_ms(void)
{
#ifdef TLSR_LAYOUT_HOST_BUILD
    uint32_t ms = host_now_ms();
#else
    uint32_t ms = clock_time() / (sys_tick_per_us * 1000u);
#endif
    if (ms == RINGLOG_EMPTY_TS) {
        ms--;
    }
    return ms;
}

static uint32_t slot_ts(uint8_t idx)
{
    uint32_t ts;
    flash_read_page(record_addr(idx), sizeof(ts), (uint8_t *)&ts);
    return ts;
}

void ringlog_init(void)
{
    g_write_idx = 0;
    g_count = 0;
    for (uint8_t i = 0; i < RINGLOG_CAPACITY; i++) {
        if (slot_ts(i) == RINGLOG_EMPTY_TS) {
            g_write_idx = i;
            g_count = i;
            g_initialised = 1;
            return;
        }
    }
    g_write_idx = 0;
    g_count = RINGLOG_CAPACITY;
    g_initialised = 1;
}

static void ensure_init(void)
{
    if (!g_initialised) {
        ringlog_init();
    }
}

void ringlog_append(uint8_t err_code, uint32_t data_ver, uint16_t partial_off, const char *message)
{
    ensure_init();

    if (g_write_idx == 0 && slot_ts(0) != RINGLOG_EMPTY_TS) {
        flash_erase_sector(RINGLOG_ADDR);
        g_count = 0;
    }

    ringlog_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.ts = now_ms();
    rec.err_code = err_code;
    rec.data_ver = data_ver;
    rec.partial_json_off = partial_off;
    if (message != NULL) {
        size_t n = strlen(message);
        if (n > sizeof(rec.message) - 1) {
            n = sizeof(rec.message) - 1;
        }
        memcpy(rec.message, message, n);
    }

    flash_write_page(record_addr(g_write_idx), sizeof(rec), (uint8_t *)&rec);
    g_write_idx = (uint8_t)((g_write_idx + 1u) % RINGLOG_CAPACITY);
    if (g_count < RINGLOG_CAPACITY) {
        g_count++;
    }
}

int ringlog_read(uint8_t idx_from_newest, ringlog_record_t *out)
{
    ensure_init();
    if (out == NULL || idx_from_newest >= g_count) {
        return -1;
    }
    uint8_t idx = (uint8_t)((g_write_idx + RINGLOG_CAPACITY - 1u - idx_from_newest) % RINGLOG_CAPACITY);
    flash_read_page(record_addr(idx), sizeof(*out), (uint8_t *)out);
    return out->ts == RINGLOG_EMPTY_TS ? -1 : 0;
}

uint8_t ringlog_count(void)
{
    ensure_init();
    return g_count;
}
