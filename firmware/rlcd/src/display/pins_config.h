#pragma once

// Waveshare ESP32-S3-RLCD-4.2 pin map.
// Sources: the vendor Arduino demo (08_LVGL_V8_Test/user_config.h for the
// ST7305 + I2C pins, 03_ADC_Test for battery, button_bsp.c for the buttons).
// Board: ESP32-S3-WROOM-1-N16R8 (16 MB flash / 8 MB PSRAM).

// --- ST7305 4.2" mono reflective LCD, SPI (landscape 400x300, 1bpp) ---------
#define ST7305_SCK   11
#define ST7305_MOSI  12
#define ST7305_DC    5
#define ST7305_CS    40
#define ST7305_RST   41
// TE (tearing-effect) is wired to GPIO6 but the panel signals it via cmd 0x35;
// we don't poll it (full-frame push at 30 s cadence makes tearing a non-issue).
#define ST7305_TE    6

// --- shared I2C bus: SHTC3 (0x70) + PCF85063 RTC (0x51) [+ audio codec] ------
#define I2C_SDA      13
#define I2C_SCL      14

// --- battery sense: ADC1_CH3 on GPIO4, behind a 3:1 resistor divider --------
#define PIN_BAT_VOLT 4

// --- side buttons (both active-low) -----------------------------------------
// BOOT doubles as the download-mode strap but is safe as a runtime button (it
// only enters download mode when held across a reset).
#define PIN_BUTTON_1 0   // BOOT
#define PIN_BUTTON_2 18  // KEY
