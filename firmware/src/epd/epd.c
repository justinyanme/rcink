#include <stdint.h>

#include "etime.h"
#include "tl_common.h"
#include "epd.h"
#include "epd_private.h"

#include "battery.h"
#include "app_config.h"

#include "epd_spi.h"
#include "drivers.h"

#include "log.h"
#include "ble.h"
#include "vendor_include.h"
#include "OneBitDisplay.h"
#include "assets/font16.h"
#include "assets/font16zh.h"
#include "assets/font40.h"

extern const uint8_t ucMirror[];

#if defined(EPD_REFRESH_STRATEGY_FORCE_FULL) && defined(EPD_REFRESH_STRATEGY_SMART)
#error "Multiple EPD refresh strategies enabled; choose exactly one."
#endif

#if !defined(EPD_REFRESH_STRATEGY_FORCE_FULL) && !defined(EPD_REFRESH_STRATEGY_SMART)
#error "No EPD refresh strategy defined; set EPD_REFRESH_STRATEGY in the build."
#endif

#define EPD_MAX_CONSECUTIVE_PARTIAL_REFRESH 10
#define EPD_UPDATE_TIMEOUT_MS 25000u

#if EPD_MAX_CONSECUTIVE_PARTIAL_REFRESH > UINT8_MAX
#error "EPD_MAX_CONSECUTIVE_PARTIAL_REFRESH must be <= UINT8_MAX for the streak counter"
#endif

RAM bool epd_is_updating = false;
RAM uint32_t epd_update_start_ms = 0;

RAM uint8_t hour_refresh = 100;
RAM uint8_t minute_refresh = 100;

RAM uint8_t epd_temperature_is_read = 0;
RAM uint8_t epd_temperature = 0;

RAM EPD_REFRESH_MODE force_refresh_when_available_mode = EPD_RM_NONE;
RAM uint8_t epd_partial_refresh_streak = 0;
RAM bool epd_pending_frame_has_red = false;

static bool is_full_frame_region(const int width, const int height, const int left, const int top) {
    return (width == EPD_WIDTH) && (height == EPD_HEIGHT) && (left == 0) && (top == 0);
}

static bool buffer_has_nonzero_bytes(const uint8_t *buffer, const int size) {
    if (buffer == NULL || size <= 0) {
        return false;
    }

    for (int offset = 0; offset < size; offset++) {
        if (buffer[offset] != 0x00) {
            return true;
        }
    }

    return false;
}

#if defined(EPD_REFRESH_STRATEGY_FORCE_FULL)
static bool should_promote_to_full_refresh(const bool requested_full_refresh, const bool frame_has_red) {
    (void)requested_full_refresh;
    (void)frame_has_red;
    return true;
}
#elif defined(EPD_REFRESH_STRATEGY_SMART)
static bool should_promote_to_full_refresh(const bool requested_full_refresh, const bool frame_has_red) {
    if (requested_full_refresh) {
        return true;
    }
    if (frame_has_red) {
        log_debugf("detected red pixels; promoting to full refresh");
        return true;
    }
    if (epd_partial_refresh_streak >= EPD_MAX_CONSECUTIVE_PARTIAL_REFRESH) {
        log_debugf(
            "partial refresh streak %d reached limit %d; forcing full refresh",
            epd_partial_refresh_streak,
            EPD_MAX_CONSECUTIVE_PARTIAL_REFRESH
        );
        return true;
    }
    return false;
}
#endif

static void note_refresh_result(const bool full_refresh) {
    if (full_refresh) {
        epd_partial_refresh_streak = 0;
        return;
    }

    // Saturate at UINT8_MAX; should_promote_to_full_refresh consumes the real threshold.
    if (epd_partial_refresh_streak < UINT8_MAX) {
        epd_partial_refresh_streak++;
    }
}

static uint32_t epd_now_ms(void) {
    return clock_time() / (sys_tick_per_us * 1000u);
}

RAM u8 epd_buffer[EPD_BUFFER_SIZE];
RAM u8 epd_buffer_red[EPD_BUFFER_SIZE];
OBDISP obd; // virtual display structure
char text[MAX_TEXT_SIZE];

RAM EPD_SCENE _scene = EPD_SCENE_DEFAULT;

_attribute_ram_code_ uint8_t EPD_read_temp(void) {
    if (epd_temperature_is_read)
        return epd_temperature;

    EPD_SPI_init();
    EPD_SPI_Hard_Reset();

    TIME_COST_START();
    if (!EPD_read_temp_impl(&epd_temperature)) {
        log_debugf("read temperature failed");
        return epd_temperature;
    }
    TIME_COST_END("READ TEMPERATURE");
    log_debugf("read temperature: %d", epd_temperature);

    epd_temperature_is_read = 1;

    return epd_temperature;
}

_attribute_ram_code_ bool EPD_Display(
    unsigned char *image, unsigned char *red_image, const bool requested_full_refresh) {
    const bool frame_has_red = buffer_has_nonzero_bytes(red_image, EPD_FRAME_BYTES);
    const bool full_refresh = should_promote_to_full_refresh(requested_full_refresh, frame_has_red);

    log_debugf(
        "EPD_Display: request_full=%d frame_has_red=%d streak=%d -> full=%d",
        requested_full_refresh,
        frame_has_red,
        epd_partial_refresh_streak,
        full_refresh
    );

    epd_is_updating = true;
    epd_update_start_ms = epd_now_ms();
    epd_pending_frame_has_red = false;

    EPD_SPI_init();
    EPD_SPI_Hard_Reset();
    uint8_t display_temperature = 0;
    if (!EPD_Display_BWR_impl(image, red_image, full_refresh, &display_temperature)) {
        epd_is_updating = false;
        layout_apply_adv_payload();
        log_debugf("EPD_Display failed");
        return false;
    }
    epd_temperature = display_temperature;
    epd_temperature_is_read = 1;
    note_refresh_result(full_refresh);
    layout_apply_adv_payload();
    return true;
}


_attribute_ram_code_ void epd_set_sleep(void) {
    EPD_set_sleep_impl();

    epd_is_updating = false;
    layout_apply_adv_payload();

    log_debugf("EPD set to sleep");
}

_attribute_ram_code_ void EPD_WriteRam(const uint8_t *image, const int width, const int height, const int left, const int top, const uint8_t is_red) {
    if (width <= 0 || height <= 0 || left < 0 || top < 0 ||
        left + width > EPD_WIDTH || top + height > EPD_HEIGHT)
    {
        log_debugf(
            "invalid EPD RAM region: width=%d height=%d left=%d top=%d",
            width, height, left, top
        );
        return;
    }

    if (is_red > 1) {
        log_debugf("invalid EPD RAM color plane: %d", is_red);
        return;
    }

    const int size = EPD_REGION_BYTE_SIZE(width, height);

    if (is_red) {
        const bool covers_full_frame = is_full_frame_region(width, height, left, top);
        if (covers_full_frame) {
            epd_pending_frame_has_red = image && buffer_has_nonzero_bytes(image, size);
        } else if (image && !epd_pending_frame_has_red) {
            epd_pending_frame_has_red = buffer_has_nonzero_bytes(image, size);
        }
    }

    EPD_WriteRam_impl(image, width, height, left, top, is_red);
}


_attribute_ram_code_ void EPD_Trigger_Display(const EPD_REFRESH_MODE refresh_mode) {
    if (refresh_mode == EPD_RM_NONE) {
        log_debugf("EPD_Trigger_Display: no refresh mode");
        return;
    }
    if (refresh_mode != EPD_RM_FULL && refresh_mode != EPD_RM_PARTIAL) {
        log_debugf("invalid refresh mode: %d", refresh_mode);
        return;
    }

    const bool requested_full = (refresh_mode == EPD_RM_FULL);
    const bool frame_has_red = epd_pending_frame_has_red;
    const bool full_refresh = should_promote_to_full_refresh(requested_full, frame_has_red);

    epd_is_updating = TRUE;
    epd_update_start_ms = epd_now_ms();
    layout_apply_adv_payload();

    log_debugf(
        "EPD_Trigger_Display: request=%d frame_has_red=%d streak=%d -> full=%d",
        refresh_mode,
        frame_has_red,
        epd_partial_refresh_streak,
        full_refresh
    );

    if (full_refresh) {
        EPD_trigger_display_impl(TRUE);
    } else {
        EPD_Load_LUT();
        EPD_trigger_display_impl(FALSE);
    }

    epd_pending_frame_has_red = false;
    note_refresh_result(full_refresh);
}

void set_force_refresh_when_available(const EPD_REFRESH_MODE mode) {
    if (mode == EPD_RM_NONE) {
        log_debugf("ignoring request to clear force refresh via NONE mode");
        return;
    }
    if (mode != EPD_RM_FULL && mode != EPD_RM_PARTIAL) {
        log_debugf("invalid force refresh mode: %d", mode);
        return;
    }

    log_debugf("trying to set force refresh mode: %d -> %d", force_refresh_when_available_mode, mode);

    const bool no_pending_request = (force_refresh_when_available_mode == EPD_RM_NONE);
    const bool higher_priority = (mode < force_refresh_when_available_mode);
    if (no_pending_request || higher_priority) {
        force_refresh_when_available_mode = mode;
    }
}

unsigned char epd_scene_is_supported(const EPD_SCENE scene) {
    switch (scene) {
    case EPD_SCENE_IMAGE:
        return true;
#if EPD_HAS_SCENE_TIME
    case EPD_SCENE_TIME:
        return true;
#endif
#if EPD_HAS_SCENE_LIST
    case EPD_SCENE_LIST:
        return true;
#endif
    default:
        return false;
    }
}

void change_scene(const EPD_SCENE scene) {
    if (!epd_scene_is_supported(scene)) {
        log_debugf("invalid scene: %d", scene);
        return;
    }
    _scene = scene;
}

_attribute_ram_code_ bool epd_check_is_updating(void) {
    if (!epd_is_updating) {
        return false;
    }
    if (!EPD_SPI_IS_BUSY()) {
        epd_set_sleep();
    } else if ((uint32_t)(epd_now_ms() - epd_update_start_ms) > EPD_UPDATE_TIMEOUT_MS) {
        log_debugf("EPD busy watchdog after %dms", EPD_UPDATE_TIMEOUT_MS);
        epd_set_sleep();
    }
    return epd_is_updating;
}

_attribute_ram_code_ bool epd_is_display_busy(void) {
    return epd_is_updating;
}

_attribute_ram_code_ void FixBuffer() {
    // Mirror panel bit order; black plane is inverted by the controller.
    for (int i = 0; i < EPD_BUFFER_SIZE; i++) {
        const uint8_t black_byte = epd_buffer[i];
        epd_buffer[i] = ~ucMirror[black_byte];

        const uint8_t red_byte = epd_buffer_red[i];
        epd_buffer_red[i] = ucMirror[red_byte];
    }
}

_attribute_ram_code_ void epd_clear_buffer(void) {
    memset(epd_buffer, 0x00, EPD_BUFFER_SIZE);
    memset(epd_buffer_red, 0x00, EPD_BUFFER_SIZE);
}

void epd_fill_ram(const EPD_COLOR color) {
    log_debugf("fill ram with color: %d", color);

    switch (color) {
    case EPD_COLOR_WHITE:
        EPD_WriteRam(NULL, EPD_WIDTH, EPD_HEIGHT, 0, 0, 0);
        EPD_WriteRam(NULL, EPD_WIDTH, EPD_HEIGHT, 0, 0, 1);
        break;
    case EPD_COLOR_BLACK:
        memset(epd_buffer, 0x00, EPD_BUFFER_SIZE);
        EPD_WriteRam(epd_buffer, EPD_WIDTH, EPD_HEIGHT, 0, 0, 0);
        EPD_WriteRam(NULL, EPD_WIDTH, EPD_HEIGHT, 0, 0, 1);
        break;
    case EPD_COLOR_RED:
        memset(epd_buffer, 0xff, EPD_BUFFER_SIZE);
        EPD_WriteRam(NULL, EPD_WIDTH, EPD_HEIGHT, 0, 0, 0);
        EPD_WriteRam(epd_buffer, EPD_WIDTH, EPD_HEIGHT, 0, 0, 1);
        break;
    default:
        log_debugf("invalid fill color: %d", color);
        break;
    }
}


void _epd_display_with_scene(const struct date_time _time, const uint16_t battery_mv, const int16_t temperature, const bool full_refresh) {
    switch (_scene) {
#if EPD_HAS_SCENE_TIME
    case EPD_SCENE_TIME:
        epd_display_time_with_date(_time, battery_mv, temperature, full_refresh);
        break;
#endif
#if EPD_HAS_SCENE_LIST
    case EPD_SCENE_LIST:
        epd_display_list(text, temperature, _time, full_refresh);
        break;
#endif
    case EPD_SCENE_IMAGE:
        // static image, already displayed, do nothing when tick
        break;
    default:
        log_debugf("warning: unknown scene %d", _scene);
#if EPD_HAS_SCENE_TIME
        epd_display_time_with_date(_time, battery_mv, temperature, full_refresh);
#elif EPD_HAS_SCENE_LIST
        epd_display_list(text, temperature, _time, full_refresh);
#endif
        break;
    }
}

void _epd_try_update(const struct date_time _time, const uint16_t battery_mv, const int16_t temperature) {
    if (epd_is_updating) {
        return;
    }

    if (force_refresh_when_available_mode != EPD_RM_NONE) {
        const bool request_full = (force_refresh_when_available_mode == EPD_RM_FULL);
        _epd_display_with_scene(_time, battery_mv, temperature, request_full);
        force_refresh_when_available_mode = EPD_RM_NONE;

        return;
    }

    bool request_full = false;
#if !DEBUG
    if (_time.tm_hour != hour_refresh) {
        hour_refresh = _time.tm_hour;
        request_full = true;
    }

    // only update when minute changed unless we owe the hourly refresh
    if (!request_full && _time.tm_min == minute_refresh) {
        return;
    }
#else
    if (_time.tm_hour != hour_refresh) {
        hour_refresh = _time.tm_hour;
        request_full = true;
    }
#endif
    minute_refresh = _time.tm_min;

    _epd_display_with_scene(_time, battery_mv, temperature, request_full);
}

bool epd_try_update() {
    if (epd_check_is_updating()) {
        return false;
    }

    if (current_battery_mv <= 0) {
        // seems not ready
        return false;
    }

    // check too often, but if refresh mode is partial or more lighter, just continue
    if (!check_time_period_reached(TIMER_C_EPD_UPDATE) && force_refresh_when_available_mode == EPD_RM_NONE) {
        return false;
    }

    const u8 temperature = EPD_read_temp();

    update_adv_data(temperature * 10, current_battery_mv);
    _epd_try_update(get_time(), current_battery_mv, temperature);

    return true;
}
