// Product fonts: the production VT323 headers, compiled against shim/M5GFX.h.
// They guard on ARDUINO; host builds define it only around these includes.
// Include from one translation unit (the headers define static arrays).
#pragma once

#ifndef ARDUINO
#define ARDUINO 1
#define RCLAB_FONTS_UNDEF_ARDUINO
#endif
#include "../../papercolor/src/render/fonts/vt323_13pt7b.h"
#include "../../papercolor/src/render/fonts/vt323_16pt7b.h"
#include "../../papercolor/src/render/fonts/vt323_18pt7b.h"
#include "../../papercolor/src/render/fonts/vt323_24pt7b.h"
#ifdef RCLAB_FONTS_UNDEF_ARDUINO
#undef ARDUINO
#undef RCLAB_FONTS_UNDEF_ARDUINO
#endif
