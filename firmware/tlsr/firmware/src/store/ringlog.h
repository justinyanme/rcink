#ifndef TLSR_LAYOUT_STORE_RINGLOG_H
#define TLSR_LAYOUT_STORE_RINGLOG_H

#include <stdint.h>

#define RINGLOG_ADDR         0x72000u
#define RINGLOG_RECORD_SIZE  64u
#define RINGLOG_CAPACITY     8u
#define RINGLOG_EMPTY_TS     0xFFFFFFFFu

typedef struct {
    uint32_t ts;
    uint8_t err_code;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t data_ver;
    uint16_t partial_json_off;
    uint8_t message[50];
} __attribute__((packed)) ringlog_record_t;

typedef char ringlog_record_t_must_be_64_bytes[(sizeof(ringlog_record_t) == RINGLOG_RECORD_SIZE) ? 1 : -1];

void ringlog_init(void);
void ringlog_append(uint8_t err_code, uint32_t data_ver, uint16_t partial_off, const char *message);
int ringlog_read(uint8_t idx_from_newest, ringlog_record_t *out);
uint8_t ringlog_count(void);

#endif
