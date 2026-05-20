#include "app_config.h"
#include "tl_common.h"
#include "led.h"

_attribute_ram_code_ void led_set_color(LED_COLOR color) {
#if UI_LED_ENABLE
    gpio_write(GPIO_LED_RED, color & BIT(_RGB_F_RED) ? LED_ON_LEVEL : !LED_ON_LEVEL);
    gpio_write(GPIO_LED_GREEN, color & BIT(_RGB_F_GREEN) ? LED_ON_LEVEL : !LED_ON_LEVEL);
    gpio_write(GPIO_LED_BLUE, color & BIT(_RGB_F_BLUE) ? LED_ON_LEVEL : !LED_ON_LEVEL);
#else
    (void)color;
#endif
}

_attribute_ram_code_ void led_blink_color(LED_COLOR color) {
#if UI_LED_ENABLE
    static bool on = false;
    on = !on;
    led_set_color(on ? color : LED_COLOR_OFF);
#else
    (void)color;
#endif
}
