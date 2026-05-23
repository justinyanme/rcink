#include "icons.h"

#include "icons_data.h"

#include <stddef.h>
#include <string.h>

static const icon_def_t icons[] = {
    {"cpu", 16, 16, icon_cpu_16},
    {"clock", 16, 16, icon_clock_16},
    {"battery", 16, 16, icon_battery_16},
    {"wifi", 16, 16, icon_wifi_16},
    {"check", 16, 16, icon_check_16},
    {"cross", 16, 16, icon_cross_16},
};

const icon_def_t *icon_lookup(const char *name, uint8_t name_len)
{
    for (size_t i = 0; i < sizeof(icons) / sizeof(icons[0]); i++) {
        const size_t n = strlen(icons[i].name);
        if (n == name_len && memcmp(name, icons[i].name, n) == 0) {
            return &icons[i];
        }
    }
    return NULL;
}
