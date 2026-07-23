#pragma once
#include <cstdint>

namespace rcpaper {
// Bottom strip height in pixels. Page layout viewport ends above this.
constexpr int16_t BOT_STRIP_H = 22;
}  // namespace rcpaper

#ifdef ARDUINO
#include <vector>
#include <M5GFX.h>

#include "net/axiom.h"

namespace rcpaper {
// Single-row strip at the bottom of the canvas:
//   ◀ PREV-ID         ● ○ ○  CURRENT-ID         NEXT-ID ▶
// Drawn directly on the M5Canvas so it composes over the page tree.
void paint_indicator(M5Canvas& canvas,
                     const std::vector<PageRecord>& pages,
                     uint8_t idx);
}  // namespace rcpaper
#endif
