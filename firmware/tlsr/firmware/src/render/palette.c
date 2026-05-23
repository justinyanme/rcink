#include "palette.h"

#include <string.h>

typedef struct {
    const char *name;
    palette_t value;
} pname_t;

static const pname_t names[] = {
    // Concrete tokens (BWR-reduced; legacy: blue and yellow have no native
    // representation, so they collapse to red and black respectively).
    {"black", PAL_BLACK},
    {"white", PAL_WHITE},
    {"red", PAL_RED},
    {"yellow", PAL_BLACK},
    {"green", PAL_BLACK},
    {"blue", PAL_RED},
    // Semantic tokens (DSL v1, BWR-reduced).
    {"accent", PAL_RED},
    {"warning", PAL_RED},
    {"success", PAL_BLACK},
    {"info", PAL_BLACK},
    {"muted", PAL_BLACK},
};

palette_t palette_from_token(const char *s, size_t n)
{
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const size_t m = strlen(names[i].name);
        if (m == n && memcmp(names[i].name, s, n) == 0) {
            return names[i].value;
        }
    }
    return PAL_UNSET;
}

uint8_t palette_pixel(palette_t p)
{
    switch (p) {
    case PAL_BLACK:
        return 0;
    case PAL_RED:
        return 2;
    case PAL_UNSET:
    case PAL_WHITE:
    default:
        return 1;
    }
}
