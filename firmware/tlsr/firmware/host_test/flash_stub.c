#include <stdint.h>
#include <string.h>

#define HOST_FLASH_SIZE 0x80000u

static uint8_t HOST_FLASH[HOST_FLASH_SIZE];
static int host_flash_initialised;

static void host_flash_init(void)
{
    if (host_flash_initialised) {
        return;
    }
    memset(HOST_FLASH, 0xFF, sizeof(HOST_FLASH));
    host_flash_initialised = 1;
}

void host_flash_reset(void)
{
    memset(HOST_FLASH, 0xFF, sizeof(HOST_FLASH));
    host_flash_initialised = 1;
}

void flash_erase_sector(unsigned long addr)
{
    host_flash_init();
    memset(&HOST_FLASH[addr & ~0xFFFul], 0xFF, 0x1000);
}

static void host_flash_write_page(unsigned long addr, unsigned long len, unsigned char *data)
{
    host_flash_init();
    for (unsigned long i = 0; i < len; i++) {
        HOST_FLASH[addr + i] &= data[i];
    }
}

static void host_flash_read_page(unsigned long addr, unsigned long len, unsigned char *out)
{
    host_flash_init();
    memcpy(out, &HOST_FLASH[addr], len);
}

void (*flash_write_page)(unsigned long addr, unsigned long len, unsigned char *data) = host_flash_write_page;
void (*flash_read_page)(unsigned long addr, unsigned long len, unsigned char *out) = host_flash_read_page;
