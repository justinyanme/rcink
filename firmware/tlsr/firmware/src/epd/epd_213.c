#include "app_config.h"

#ifdef EPD_213
// SSD1680

#include "epd.h"
#include "epd_spi.h"
#include "epd_213.h"
#include "drivers.h"
#include "tl_common.h"
#include "log.h"

#define EPD_PAD_LEFT    46

#ifndef EPD_PAD_TOP
#error "EPD_PAD_TOP must be provided by the build system (e.g. -DEPD_PAD_TOP=8)"
#endif

#if EPD_PAD_TOP < 0
#error "EPD_PAD_TOP cannot be negative; pick 0 for FPC-A002 or 8 for FPC-7528B"
#endif

#if (EPD_PAD_TOP % 8) != 0
#error "EPD_PAD_TOP must be byte aligned"
#endif

// Screen guide: type 1 (FPC-7528B) uses 8 to avoid a broken bottom row. Type 2 (FPC-A002) uses 0 to keep the top row visible.

// https://www.mdpi.com/2072-666X/12/5/578
#define VSS  (0b00)
#define VH1  (0b01)
#define VSL  (0b10)
#define VH2  (0b11)
#define V(a,b,c,d)  (a<<6|b<<4|c<<2|d)

#define W___ V(VSL, VSS, VSS, VSS)
#define R___ V(VH2, VSS, VSS, VSS)
#define B___ V(VH1, VSS, VSS, VSS)
#define _B__ V(VSS, VH1, VSS, VSS)
#define _W__ V(VSS, VSL, VSS, VSS)
#define _R__ V(VSS, VH2, VSS, VSS)
#define BW__ V(VH1, VSL, VSS, VSS)
#define RW__ V(VH2, VSL, VSS, VSS)

static const uint8_t LUT_bwr_213_part[] = {
    //  VS [0-11] phase [ABCD]
    //     0     1     2     3     4     5     6     7     8     9    10    11
    B___, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT0 B
    _W__, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT1 W
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT2 R
    0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT3 NC?
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4 VCOM

    // 60:
    //  TP A, TP B, SRAB, TP C, TP D, SRCD,   RP
    0x03, 0x03, 0x01, 0x00, 0x00, 0x00, 0x02, // 0
    0x09, 0x09, 0x01, 0x00, 0x00, 0x00, 0x02, // 1
    0x03, 0x03, 0x01, 0x00, 0x00, 0x00, 0x02, // 2
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 3
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 4
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 5
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 6
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 8
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 9
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 10
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 11

    // 144: FR  25-200Hz
    0x22, 0x20, 0x00, 0x00, 0x00, 0x00,
    // 150: XON
    0x00, 0x00, 0x00,
    // 153:
    //  EOPT    VGH     VSH1    VSH2    VSL     VCOM
    //  3F      03      04                      2C
    //  22      -20v    15v     3v      -15v
    0x22, 0x17, 0x41, 0x94, 0x32, 0x36
};

#define EPD_LUT_BYTES 153
typedef char epd_lut_size_must_match[(sizeof(LUT_bwr_213_part) == EPD_LUT_BYTES + 6) ? 1 : -1];

static _attribute_ram_code_ bool epd_soft_reset(void) {
    EPD_SPI_WriteCmd(0x12);
    return EPD_SPI_WaitBusy(100);
}

void EPD_trigger_display_impl(const bool full_refresh) {
    log_debugf("EPD_trigger_display_impl: %d", full_refresh);
    // Display update control
    EPD_SPI_WriteCmd(0x22);
    EPD_SPI_WriteData(full_refresh ? 0xF7 : 0xC7);
    // Master Activation
    EPD_SPI_WriteCmd(0x20);
}

void EPD_WriteRam_impl(const uint8_t *image, const int width, const int height, const int left, const int top, const uint8_t is_red) {
    // data size in bytes
    const int size = EPD_REGION_BYTE_SIZE(width, height);
    const int padded_top = top + EPD_PAD_TOP;

    if ((padded_top % 8) != 0) {
        log_debugf("EPD RAM top is not byte aligned: top=%d pad=%d", top, EPD_PAD_TOP);
        return;
    }

    // Set Ram X address
    uint8_t x = (uint8_t)(padded_top / 8);
    EPD_SPI_WriteCmd(0x4E);
    EPD_SPI_WriteData(x & 0xFF);

    // Set Ram Y address
    uint16_t y = left + EPD_PAD_LEFT;
    EPD_SPI_WriteCmd(0x4F);
    EPD_SPI_WriteData(y & 0xFF);
    EPD_SPI_WriteData(y >> 8 & 0xFF);

    const uint8_t reg = is_red ? 0x26 : 0x24; // BW: 0x24, Red: 0x26
    if (image) {
        EPD_SPI_LoadImage(image, size, reg);
    } else {
        EPD_SPI_WriteCmd(reg);
        for (int i = 0; i < size; i++) {
            EPD_SPI_WriteData(is_red ? 0x00 : 0xff);
        }
    }
}


_attribute_ram_code_ bool EPD_read_temp_impl(uint8_t *out_temperature) {
    // Temperature sensor control
    EPD_SPI_WriteCmd(0x18);
    EPD_SPI_WriteData(0x80); // 80: internal sensor 48: external sensor

    // Display update control
    EPD_SPI_WriteCmd(0x22);
    EPD_SPI_WriteData(0xB1); // no display? // the original code is 1B

    // Master Activation
    EPD_SPI_WriteCmd(0x20);
    if (!EPD_SPI_WaitBusy(100)) {
        return false;
    }

    // read temperature
    EPD_SPI_WriteCmd(0x1B);
    if (out_temperature != NULL) {
        *out_temperature = EPD_SPI_read();
    }
    return true;
}

_attribute_ram_code_ bool EPD_Display_setup(const bool soft_reset) {
    if (soft_reset && !epd_soft_reset()) {
        return false;
    }
    // left up corner
    const int w0 = EPD_PAD_LEFT + 0;
    const int h0 = EPD_PAD_TOP + 0;

    // right bottom corner
    const int w1 = w0 + EPD_WIDTH - 1;
    const int h1 = h0 + EPD_HEIGHT - 1;

    // Border Waveform
    EPD_SPI_WriteCmd(0x3C);
    EPD_SPI_WriteData(0x05);

    // Driver output control
    EPD_SPI_WriteCmd(0x01);
    EPD_SPI_WriteData(0x27); // mux=0x127(296)
    EPD_SPI_WriteData(0x01);
    EPD_SPI_WriteData(0x01); // gd=0, sm=0, tb=1

    // Data entry mode
    EPD_SPI_WriteCmd(0x11);
    EPD_SPI_WriteData(0x07); // am=1, id=11

    // set display window
    // Set RAM X Address Start/End
    EPD_SPI_WriteCmd(0x44);
    EPD_SPI_WriteData(h0 >> 3 & 0xff);
    EPD_SPI_WriteData(h1 >> 3 & 0xff);

    // Set RAM Y Address Start/End
    EPD_SPI_WriteCmd(0x45); //set Ram-Y address start/end position
    EPD_SPI_WriteData(w0 & 0xff);
    EPD_SPI_WriteData(w0 >> 8 & 0xff);
    EPD_SPI_WriteData(w1 & 0xff);
    EPD_SPI_WriteData(w1 >> 8 & 0xff);
    return true;
}

// only supports one lut for now, could be extended to support more
_attribute_ram_code_ void EPD_Load_LUT() {
    // load lut
    EPD_SPI_WriteCmd(0x32);
    for (int i = 0; i < EPD_LUT_BYTES; i++) {
        EPD_SPI_WriteData(LUT_bwr_213_part[i]);
    }

    // load more settings
    // gate voltage
    EPD_SPI_WriteCmd(0x3F);
    EPD_SPI_WriteData(LUT_bwr_213_part[EPD_LUT_BYTES]);

    EPD_SPI_WriteCmd(0x03);
    EPD_SPI_WriteData(LUT_bwr_213_part[EPD_LUT_BYTES + 1]);

    // source voltage
    EPD_SPI_WriteCmd(0x04);
    EPD_SPI_WriteData(LUT_bwr_213_part[EPD_LUT_BYTES + 2]); // VSH
    EPD_SPI_WriteData(LUT_bwr_213_part[EPD_LUT_BYTES + 3]); // VSH2
    EPD_SPI_WriteData(LUT_bwr_213_part[EPD_LUT_BYTES + 4]); // VSL

    EPD_SPI_WriteCmd(0x2C);
    EPD_SPI_WriteData(LUT_bwr_213_part[EPD_LUT_BYTES + 5]);
}

_attribute_ram_code_ bool EPD_Display_BWR_impl(
    unsigned char *image,
    unsigned char *red_image,
    const uint8_t full_refresh,
    uint8_t *out_temperature
) {
    if (!epd_soft_reset()) {
        return false;
    }

    uint8_t temperature = 0;
    if (!EPD_read_temp_impl(&temperature)) {
        return false;
    }

    if (!EPD_Display_setup(FALSE)) {
        return false;
    }

    // load lut
    if (!full_refresh) {
        EPD_Load_LUT();
    }

    EPD_WriteRam_impl(image, EPD_WIDTH, EPD_HEIGHT, 0, 0, 0);
    EPD_WriteRam_impl(red_image, EPD_WIDTH, EPD_HEIGHT, 0, 0, 1);


    EPD_trigger_display_impl(full_refresh);

    if (out_temperature != NULL) {
        *out_temperature = temperature;
    }
    return true;
}

_attribute_ram_code_ void EPD_set_sleep_impl(void) {
    // deep sleep
    EPD_SPI_WriteCmd(0x10);
    EPD_SPI_WriteData(0x01);
}

#endif
