#pragma once

typedef enum {
    _RGB_F_RED = 0,
    _RGB_F_GREEN = 1,
    _RGB_F_BLUE = 2,
} _RGB_FLAG;

typedef enum {
    LED_COLOR_OFF = 0,

    LED_COLOR_RED = BIT(_RGB_F_RED),
    LED_COLOR_GREEN = BIT(_RGB_F_GREEN),
    LED_COLOR_BLUE = BIT(_RGB_F_BLUE),

    LED_COLOR_YELLOW = BIT(_RGB_F_RED) | BIT(_RGB_F_GREEN),
    LED_COLOR_CYAN = BIT(_RGB_F_GREEN) | BIT(_RGB_F_BLUE),
    LED_COLOR_PURPLE = BIT(_RGB_F_RED) | BIT(_RGB_F_BLUE),

    LED_COLOR_WHITE = BIT(_RGB_F_RED) | BIT(_RGB_F_GREEN) | BIT(_RGB_F_BLUE),
} LED_COLOR;


void led_set_color(LED_COLOR color);
void led_blink_color(LED_COLOR color);
