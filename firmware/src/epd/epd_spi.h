#pragma once

#include "types.h"

#define EPD_SPI_ENABLE_WRITE_CMD() gpio_write(EPD_DC, 0)
#define EPD_SPI_ENABLE_WRITE_DATA() gpio_write(EPD_DC, 1)

#define EPD_SPI_IS_BUSY() (gpio_read(EPD_BUSY))


void EPD_SPI_init(void);
void EPD_SPI_Hard_Reset();
void EPD_SPI_Write(unsigned char value);
u8 EPD_SPI_read(void);
void EPD_SPI_WriteCmd(unsigned char cmd);
void EPD_SPI_WriteData(unsigned char data);
bool EPD_SPI_WaitBusy(int max_ms);
void EPD_SPI_LoadImage(const unsigned char *image, int size, u8 cmd);
