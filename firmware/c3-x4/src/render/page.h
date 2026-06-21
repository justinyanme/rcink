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
// Xteink X4: SSD1677 800x480 B&W e-ink, landscape. Device chrome (battery,
// clock, page index, prev/next hints) lives in a right sidebar so the full
// 480px height is free for page content — same layout language as t-amoled,
// scaled up to the larger panel.
static constexpr int16_t SCREEN_W = 800;
static constexpr int16_t SCREEN_H = 480;
static constexpr int16_t SIDEBAR_W = 110;
static constexpr int16_t CONTENT_W = SCREEN_W - SIDEBAR_W;  // 690

void display_init();
void display_sleep();
void clear_screen();

// Free / rebuild the 48KB compose sprite around the memory-heavy Axiom fetch on
// the PSRAM-less C3 (see render/page.cpp and fetch_and_restart in main.cpp).
void display_release_canvas();
void display_acquire_canvas();

bool parse_page_layout(const PageRecord& rec, LayoutNode& root);
void render_page(const PageRecord& rec, const LayoutNode& root, bool stale,
                 const DeviceState& state, const BatteryReading& batt);
void render_error(const char* msg);

// Transient centred status (e.g. "Fetching data...") shown during the
// few-second fetch so the user sees progress instead of a stale page.
void render_status(const char* msg);

// Dump the current 1-bit compose buffer over serial (hex, marker-framed) for
// headless verification via tools/fb_to_png.py.
void dump_framebuffer();
#endif

}  // namespace rcpaper
