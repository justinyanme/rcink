#pragma once

#include "proto/schema.h"
#include "display/power_policy.h"

#ifdef ARDUINO
#include "proto/envelope.h"
#include "sense/battery.h"
#include "sense/shtc3.h"
#include "store/nvs.h"
#endif

namespace rcpaper {

bool layout_has_remote_image(const LayoutNode& node);
bool layout_has_any_image(const LayoutNode& node);

#ifdef ARDUINO
// Waveshare 3.97" e-paper, driven landscape 800x480 (4-level grayscale). The
// dashboard is a fixed hand-drawn screen; the Axiom page-deck reuses the layout
// engine with a right sidebar (battery/clock/nav) like the AMOLED target.
static constexpr int16_t SCREEN_W = 800;
static constexpr int16_t SCREEN_H = 480;
static constexpr int16_t SIDEBAR_W = 110;
static constexpr int16_t CONTENT_W = SCREEN_W - SIDEBAR_W;  // 690

// Resume startup initializes the controller without changing retained glass.
void display_init(PanelStartup startup);
void display_sleep();
void clear_screen();

// Hand-drawn consumer dashboard: top bar (title + clock + date), big indoor
// temperature hero, humidity + bar, bottom battery strip. `epoch` is local
// wall-clock seconds (0 => clock shows --:--).
void render_dashboard(const Shtc3Reading& env, const BatteryReading& batt,
                      uint64_t epoch);

bool parse_page_layout(const PageRecord& rec, LayoutNode& root);
void render_page(const PageRecord& rec, const LayoutNode& root, bool stale,
                 const DeviceState& state, const BatteryReading& batt);
void render_error(const char* msg);

// Transient centred status (e.g. "Fetching data...") shown during the
// few-second fetch so the user sees progress instead of a stale page.
void render_status(const char* msg);
// Always logs fetch progress; only dev renders costly transient e-paper frames.
void report_fetch_status(const char* msg);
#endif

}  // namespace rcpaper
