#include "etime.h"
#include "tl_common.h"
#include "epd.h"
#include "epd_private.h"

#include "battery.h"
#include "app_config.h"

#include "ble.h"
#include "vendor_include.h"
#include "OneBitDisplay.h"

#include "assets/asset.h"


extern uint8_t mac_public[6];
extern u8 epd_buffer[EPD_BUFFER_SIZE];
extern u8 epd_buffer_red[EPD_BUFFER_SIZE];
extern OBDISP obd;

void epd_display_time_with_date(
    struct date_time _time, uint16_t battery_mv, int16_t temperature,
    const bool full_refresh
) {
    epd_clear_buffer();

    obdCreateVirtualDisplay(&obd, EPD_WIDTH, EPD_HEIGHT, epd_buffer);
    obdFill(&obd, 0, 0); // fill with white

    char buff[32];
    const u16 battery_level = mv_to_battery_level(battery_mv);

    sprintf(buff, "TPT_%02X%02X%02X", mac_public[2], mac_public[1], mac_public[0]);
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16, 10, 17, (char*)buff, 1);

    if (ble_is_connected()) {
        sprintf(buff, "%s", "234");
    } else {
        sprintf(buff, "%s", "56");
    }

    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16_zh, 118, 21, (char*)buff, 1);

    // battery shape
    obdRectangle(&obd, 212, 10, 215, 14, 1, 1);
    obdRectangle(&obd, 215, 2, 249, 22, 1, 1);

    sprintf(buff, "%d", battery_level);
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16, 216, 18, (char*)buff, 0);

    // display time
    sprintf(buff, "%02d:%02d", _time.tm_hour, _time.tm_min);
    obdWriteStringCustom(&obd, (GFXfont*)&DSEG14_Classic_Mini_Regular_40, 10, 85, (char*)buff, 1);

    sprintf(buff, "    %d°C", temperature);
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16, 170, 50, (char*)buff, 1);

    sprintf(buff, " %dmV", battery_mv);
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16, 170, 84, (char*)buff, 1);

    obdRectangle(&obd, 168, 27, 170, 99, 1, 1);

    sprintf(buff, "%04d-%02d-%02d", _time.tm_year, _time.tm_month, _time.tm_day);
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16, 8, 118, (char*)buff, 1);

    // week
    if (_time.tm_week == 6) { // sunday
        sprintf(buff, "9:%c", 0x20 + 13);
    } else {
        // 0x21: 一 [1]
        sprintf(buff, "9:%c", _time.tm_week + 1 + 0x20);
    }
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16_zh, 105, 122, (char*)buff, 1);

    if (_time.tm_hour > 7 && _time.tm_hour < 20) {
        sprintf(buff, "%s", "EFGH");
    } else {
        sprintf(buff, "%s", "ABCD");
    }
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16_zh, 165, 122, (char*)buff, 1);

    FixBuffer();

    EPD_Display(epd_buffer, epd_buffer_red, full_refresh);
}
