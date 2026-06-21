#pragma once

// Xteink X4 — ESP32-C3 + SSD1677 / GDEQ0426T82 4.26" 800x480 B&W e-ink.
// Pin map lifted from the crosspoint-reader open-x4-sdk HalGPIO.h (X4 variant).

// --- E-ink SPI (custom pins, not the C3 hardware-SPI defaults) ---
#define EPD_SCLK   8   // SPI clock
#define EPD_MOSI   10  // SPI MOSI
#define EPD_CS     21  // chip select
#define EPD_DC     4   // data/command
#define EPD_RST    5   // reset
#define EPD_BUSY   6   // busy (active HIGH on the X4 / SSD1677)
#define EPD_MISO   7   // shared with SD card; unused by the display

// --- Battery sense ---
// GPIO0 ADC behind a 2.0x divider (battery.cpp doubles the reading). GPIO0 is
// also the C3 boot strapping pin; reading it as ADC at runtime is fine.
#define PIN_BAT_VOLT  0

// --- Power / USB ---
#define PIN_BATTERY_LATCH 13  // MOSFET latch; held LOW in deep sleep to keep power
#define PIN_USB_DETECT    20  // HIGH when USB is connected (X4)

// --- Buttons ---
// The X4 has no plain GPIO buttons: six buttons sit on two ADC ladders, plus a
// hard-wired power button. Thresholds are read in main.cpp (ported from the
// crosspoint InputManager). Defined here so the pin map lives in one place.
#define PIN_BTN_ADC_1  1
#define PIN_BTN_ADC_2  2
#define PIN_BTN_POWER  3
