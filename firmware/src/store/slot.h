#ifndef TLSR_LAYOUT_STORE_SLOT_H
#define TLSR_LAYOUT_STORE_SLOT_H

#include <stddef.h>
#include <stdint.h>

#define SLOT_A_ADDR          0x74000u
#define SLOT_B_ADDR          0x75000u
#define SLOT_SIZE            0x1000u
#define SLOT_HEADER_SIZE     32u
#define SLOT_MAX_PAYLOAD     (SLOT_SIZE - SLOT_HEADER_SIZE)
#define SLOT_MAGIC_BE        0x54504C31u

typedef struct {
    uint32_t magic;
    uint32_t data_ver;
    uint16_t uncompressed_len;
    uint16_t compressed_len;
    uint32_t crc32;
    uint32_t render_ts;
    uint32_t seq;
    uint16_t flags;
    uint8_t reserved[6];
} slot_header_t;

typedef char slot_header_t_must_be_32_bytes[(sizeof(slot_header_t) == 32) ? 1 : -1];

typedef enum {
    SLOT_NONE,
    SLOT_A,
    SLOT_B,
} slot_id_t;

int slot_read_header(uint32_t addr, slot_header_t *out);
int slot_read_payload(uint32_t addr, uint8_t *buf, uint16_t len);
slot_id_t slot_active(slot_header_t *out_header);
slot_id_t slot_staging(void);
void slot_erase(slot_id_t slot);
int slot_write_payload(slot_id_t slot, uint16_t offset, const uint8_t *data, uint16_t len);
int slot_write_header(slot_id_t slot, const slot_header_t *hdr);
uint32_t slot_addr(slot_id_t slot);

#endif
