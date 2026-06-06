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
static constexpr int16_t SCREEN_W = 240;
static constexpr int16_t SCREEN_H = 536;
static constexpr int16_t TOP_STRIP_H = 24;
static constexpr int16_t BOT_STRIP_H = 28;

void display_init();
void display_sleep();
void clear_screen();

bool parse_page_layout(const PageRecord& rec, LayoutNode& root);
void render_page(const PageRecord& rec, const LayoutNode& root, bool stale,
                 const DeviceState& state, const BatteryReading& batt);
void render_error(const char* msg);
#endif

}  // namespace rcpaper
