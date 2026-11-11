#pragma once
#include "config.h"

#if defined(RCRLCD_USAGE_URL) != defined(RCRLCD_USAGE_READ_KEY)
#error "Configure both RCRLCD_USAGE_URL and RCRLCD_USAGE_READ_KEY"
#endif
#if defined(RCRLCD_USAGE_URL) && defined(RCRLCD_USAGE_READ_KEY)
#define RCRLCD_USAGE_ENABLED 1
#else
#define RCRLCD_USAGE_ENABLED 0
#endif
