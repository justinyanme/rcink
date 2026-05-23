#include "slot.h"

#include "../proto/crc32.h"

#include <string.h>

extern void flash_erase_sector(unsigned long addr);
extern void (*flash_read_page)(unsigned long addr, unsigned long len, unsigned char *buf);
extern void (*flash_write_page)(unsigned long addr, unsigned long len, unsigned char *buf);

uint32_t slot_addr(slot_id_t slot)
{
    switch (slot) {
    case SLOT_A: return SLOT_A_ADDR;
    case SLOT_B: return SLOT_B_ADDR;
    case SLOT_NONE:
    default: return 0;
    }
}

static int slot_id_from_addr(uint32_t addr, slot_id_t *out)
{
    if (addr == SLOT_A_ADDR) {
        *out = SLOT_A;
        return 0;
    }
    if (addr == SLOT_B_ADDR) {
        *out = SLOT_B;
        return 0;
    }
    return -1;
}

static uint32_t slot_payload_crc(uint32_t addr, uint16_t len)
{
    uint8_t buf[128];
    uint32_t state = 0;
    uint16_t off = 0;
    while (off < len) {
        uint16_t n = (uint16_t)(len - off);
        if (n > sizeof(buf)) {
            n = sizeof(buf);
        }
        flash_read_page(addr + SLOT_HEADER_SIZE + off, n, buf);
        state = crc32_zlib_update(state, buf, n);
        off = (uint16_t)(off + n);
    }
    return crc32_zlib_finalize(state);
}

int slot_read_header(uint32_t addr, slot_header_t *out)
{
    if (out == NULL) {
        return -1;
    }
    slot_id_t slot;
    if (slot_id_from_addr(addr, &slot) != 0) {
        return -1;
    }
    (void)slot;
    flash_read_page(addr, sizeof(*out), (uint8_t *)out);
    if (out->magic != SLOT_MAGIC_BE || out->compressed_len > SLOT_MAX_PAYLOAD) {
        return -1;
    }
    if (slot_payload_crc(addr, out->compressed_len) != out->crc32) {
        return -1;
    }
    return 0;
}

int slot_read_payload(uint32_t addr, uint8_t *buf, uint16_t len)
{
    slot_id_t slot;
    if (buf == NULL || slot_id_from_addr(addr, &slot) != 0 || len > SLOT_MAX_PAYLOAD) {
        return -1;
    }
    (void)slot;
    flash_read_page(addr + SLOT_HEADER_SIZE, len, buf);
    return 0;
}

slot_id_t slot_active(slot_header_t *out_header)
{
    slot_header_t a;
    slot_header_t b;
    const int a_ok = slot_read_header(SLOT_A_ADDR, &a) == 0;
    const int b_ok = slot_read_header(SLOT_B_ADDR, &b) == 0;

    if (!a_ok && !b_ok) {
        if (out_header != NULL) {
            memset(out_header, 0, sizeof(*out_header));
        }
        return SLOT_NONE;
    }

    if (a_ok && (!b_ok || a.seq >= b.seq)) {
        if (out_header != NULL) {
            *out_header = a;
        }
        return SLOT_A;
    }

    if (out_header != NULL) {
        *out_header = b;
    }
    return SLOT_B;
}

slot_id_t slot_staging(void)
{
    slot_id_t active = slot_active(NULL);
    if (active == SLOT_A) {
        return SLOT_B;
    }
    return SLOT_A;
}

void slot_erase(slot_id_t slot)
{
    uint32_t addr = slot_addr(slot);
    if (addr != 0) {
        flash_erase_sector(addr);
    }
}

int slot_write_payload(slot_id_t slot, uint16_t offset, const uint8_t *data, uint16_t len)
{
    uint32_t addr = slot_addr(slot);
    if (addr == 0 || data == NULL || offset > SLOT_MAX_PAYLOAD || len > SLOT_MAX_PAYLOAD - offset) {
        return -1;
    }
    flash_write_page(addr + SLOT_HEADER_SIZE + offset, len, (uint8_t *)data);
    return 0;
}

int slot_write_header(slot_id_t slot, const slot_header_t *hdr)
{
    uint32_t addr = slot_addr(slot);
    if (addr == 0 || hdr == NULL || hdr->magic != SLOT_MAGIC_BE || hdr->compressed_len > SLOT_MAX_PAYLOAD) {
        return -1;
    }
    flash_write_page(addr, sizeof(*hdr), (uint8_t *)hdr);
    return 0;
}
