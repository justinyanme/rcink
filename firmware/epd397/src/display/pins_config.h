#pragma once

// Waveshare ESP32-S3-ePaper-3.97 pin map.
// Sources: the vendor Arduino demo (02_E-Paper_Example/DEV_Config.h for the EPD
// pins) and the product schematic / ESP-IDF demo for I2C, PMIC and buttons.
// Board in hand: ESP32-S3-WROOM-1-N16R8 (16 MB flash / 8 MB octal PSRAM).

// --- 3.97" e-paper (SSD1677-family), 4-wire SPI, landscape 800x480 -----------
#define EPD_SCK    11
#define EPD_MOSI   12
#define EPD_CS     10
#define EPD_DC     9
#define EPD_RST    46
#define EPD_BUSY   3   // active HIGH (HIGH = busy, LOW = idle)

// --- shared I2C bus: SHTC3 (0x70) + PCF85063 RTC (0x51) + AXP2101 (0x34) ------
#define I2C_SDA    41
#define I2C_SCL    42

// --- firmware-readable buttons (all active-low) ------------------------------
// The board exposes a 3-way wheel (Up/Function/Down) plus BOOT. We map:
//   FUNCTION (GPIO5) -> the "KEY" role (toggle dashboard <-> deck)
//   BOOT     (GPIO0) -> sensors refresh (dash) / next page (deck)
// BOOT doubles as the download strap but is safe at runtime. PWR is a hardware
// power key handled by the board power circuit, not exposed as an app GPIO.
#define PIN_BUTTON_1 0   // BOOT
#define PIN_BUTTON_2 5   // FUNCTION (wheel centre)
#define PIN_BUTTON_UP   4
#define PIN_BUTTON_DOWN 6
