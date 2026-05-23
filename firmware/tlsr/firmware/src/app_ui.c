#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app.h"
#include "app_att.h"
#include "app_ui.h"

extern u32 latest_user_event_tick;

#if (BLE_OTA_SERVER_ENABLE)
/**
 * @brief      this function is used to register the function for OTA start.
 * @param[in]  none
 * @return     none
 */
void app_enter_ota_mode(void) {
    tlkapi_send_string_data(APP_OTA_LOG_EN, "[APP][OTA] enter ota mode", 0, 0);
    ota_is_working = 1;
    latest_user_event_tick = clock_time();
}


/**
 * @brief      this function is used to register the function for OTA end.
 * @param[in]  result - OTA result
 * @return     none
 */
void app_ota_end_result(int result) {
#if(UI_LED_ENABLE)  //this is only for debug
    if (result == OTA_SUCCESS) {
        //led for debug: OTA success
        gpio_write(GPIO_LED_BLUE, 1);
        sleep_ms(300);
        gpio_write(GPIO_LED_BLUE, 0);
        sleep_ms(300);
        gpio_write(GPIO_LED_BLUE, 1);
        sleep_ms(300);
        gpio_write(GPIO_LED_BLUE, 0);

        /* attention that this log may lost, because MCU will reboot for successful OTA */
        //tlkapi_send_string_data(APP_OTA_LOG_EN, "[APP][OTA] ota success", &result, 1);
    } else {
        //OTA fail
        gpio_write(GPIO_LED_BLUE, 1);
        sleep_ms(100);
        gpio_write(GPIO_LED_BLUE, 0);

        /* attention that this log may lost */
        //tlkapi_send_string_data(APP_OTA_LOG_EN, "[APP][OTA] ota fail", &result, 1);
    }
#endif
}
#endif
