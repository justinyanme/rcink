#pragma once

#include "proto/schema.h"

#ifdef ARDUINO
#include "proto/envelope.h"
#include "sense/battery.h"
#include "store/nvs.h"
#endif

namespace rcpaper {

bool layout_has_remote_image(const LayoutNode& node);
bool layout_has_any_image(const LayoutNode& node);

#ifdef ARDUINO
// Landscape: the 1.91" panel is a 240x536 bar rotated to 536x240 (rotation 1).
// The two physical buttons sit on the right edge, so device chrome (battery,
// clock, page index, prev/next hints) lives in a right sidebar instead of
// top/bottom strips — that frees the full 240px height for page content.
static constexpr int16_t SCREEN_W = 536;
static constexpr int16_t SCREEN_H = 240;
static constexpr int16_t SIDEBAR_W = 56;
static constexpr int16_t CONTENT_W = SCREEN_W - SIDEBAR_W;  // 480

void display_init();
void display_sleep();
void clear_screen();

bool parse_page_layout(const PageRecord& rec, LayoutNode& root);
void render_page(const PageRecord& rec, const LayoutNode& root, bool stale,
                 const DeviceState& state, const BatteryReading& batt);
void render_error(const char* msg);
#endif

}  // namespace rcpaper
