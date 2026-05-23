#include "epd.h"
#include "epd_spi.h"
#include "drivers.h"
#include "vendor_include.h"
#include "tlkapi_debug.h"
#include "log.h"

_attribute_ram_code_ void EPD_SPI_init(void) {
    gpio_set_func(EPD_RESET, AS_GPIO);
    gpio_set_output_en(EPD_RESET, 1);
    gpio_setup_up_down_resistor(EPD_RESET, PM_PIN_PULLUP_1M);

    gpio_set_func(EPD_DC, AS_GPIO);
    gpio_set_output_en(EPD_DC, 1);
    gpio_setup_up_down_resistor(EPD_DC, PM_PIN_PULLUP_1M);

    gpio_set_func(EPD_BUSY, AS_GPIO);
    gpio_set_output_en(EPD_BUSY, 0);
    gpio_set_input_en(EPD_BUSY, 1);
    gpio_setup_up_down_resistor(EPD_BUSY, PM_PIN_PULLUP_1M);

    gpio_set_func(EPD_CS, AS_GPIO);
    gpio_set_output_en(EPD_CS, 1);
    gpio_setup_up_down_resistor(EPD_CS, PM_PIN_PULLUP_1M);

    gpio_set_func(EPD_CLK, AS_GPIO);
    gpio_set_output_en(EPD_CLK, 1);
    gpio_setup_up_down_resistor(EPD_CLK, PM_PIN_PULLUP_1M);

    gpio_set_func(EPD_MOSI, AS_GPIO);
    gpio_set_output_en(EPD_MOSI, 1);
    gpio_setup_up_down_resistor(EPD_MOSI, PM_PIN_PULLUP_1M);
}

_attribute_ram_code_ void EPD_SPI_Hard_Reset() {
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);
}

_attribute_ram_code_ void EPD_SPI_Write(unsigned char value) {
    WaitUs(10);
    for (unsigned char i = 0; i < 8; i++) {
        gpio_write(EPD_CLK, 0);
        if (value & 0x80) {
            gpio_write(EPD_MOSI, 1);
        } else {
            gpio_write(EPD_MOSI, 0);
        }
        value = (value << 1);
        gpio_write(EPD_CLK, 1);
    }
}

_attribute_ram_code_ uint8_t EPD_SPI_read(void) {
    uint8_t value = 0;

    gpio_shutdown(EPD_MOSI);
    gpio_set_output_en(EPD_MOSI, 0);
    gpio_set_input_en(EPD_MOSI, 1);
    gpio_write(EPD_CS, 0);
    EPD_SPI_ENABLE_WRITE_DATA();
    WaitUs(10);
    for (unsigned char i = 0; i < 8; i++) {
        gpio_write(EPD_CLK, 0);
        gpio_write(EPD_CLK, 1);
        value <<= 1;
        if (gpio_read(EPD_MOSI) != 0) {
            value |= 1;
        }
    }
    gpio_set_output_en(EPD_MOSI, 1);
    gpio_set_input_en(EPD_MOSI, 0);
    gpio_write(EPD_CS, 1);
    return value;
}

_attribute_ram_code_ void EPD_SPI_WriteCmd(const unsigned char cmd) {
    gpio_write(EPD_CS, 0);
    EPD_SPI_ENABLE_WRITE_CMD();
    EPD_SPI_Write(cmd);
    gpio_write(EPD_CS, 1);
}

_attribute_ram_code_ void EPD_SPI_WriteData(const unsigned char data) {
    gpio_write(EPD_CS, 0);
    EPD_SPI_ENABLE_WRITE_DATA();
    EPD_SPI_Write(data);
    gpio_write(EPD_CS, 1);
}

_attribute_ram_code_ bool EPD_SPI_WaitBusy(const int max_ms) {
    if (max_ms <= 0) {
        log_debugf("invalid EPD busy timeout: %dms", max_ms);
        return false;
    }

    const unsigned long start_ticks = clock_time();
    const unsigned long timeout_ticks = (unsigned long)max_ms * CLOCK_16M_SYS_TIMER_CLK_1MS;
    WaitMs(1);
    while (EPD_SPI_IS_BUSY()) {
        if ((unsigned long)(clock_time() - start_ticks) >= timeout_ticks) {
            log_debugf("EPD busy wait timed out after %dms", max_ms);
            return false;
        }
    }
    return true;
}

_attribute_ram_code_ void EPD_SPI_LoadImage(const unsigned char *image, const int size, const uint8_t cmd) {
    EPD_SPI_WriteCmd(cmd);
    for (int i = 0; i < size; i++) {
        EPD_SPI_WriteData(image[i]);
    }
    WaitMs(2);
}
