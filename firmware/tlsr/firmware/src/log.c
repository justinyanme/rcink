#include "tl_common.h"
#include "log.h"
#include "string.h"

_attribute_data_retention_ u32 verbose_tick = 0;
_attribute_data_retention_ char verbose_buffer[256] = {0};

unsigned int strlen(const char *str) {
    unsigned int len = 0;
    if (str != NULL) {
        while (*str++) {
            len++;
        }
    }
    return len;
}

_attribute_ram_code_ void _log_verbose_helper(const char *fmt, ...) {
    // if less than 200ms, ignore no matter what
    if (!clock_time_exceed(verbose_tick, 200000)) {
        return;
    }

    // using buffer to store and check if its duplicate
    const bool is_duplicate = memcmp(verbose_buffer, fmt, min(strlen(fmt), 255)) == 0;
    // only skip if its duplicate and less than 5s
    if (is_duplicate && !clock_time_exceed(verbose_tick, 5000000)) {
        return;
    }

    if (!is_duplicate) {
        memcpy(verbose_buffer, fmt, min(strlen(fmt), 255));
    }
    verbose_tick = clock_time();

    va_list args;
    va_start(args, fmt);
    v_printf(fmt, args);
    va_end(args);
}
