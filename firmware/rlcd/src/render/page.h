#pragma once

#include "display/power_policy.h"
#include "power/runtime_policy.h"
#include "proto/schema.h"

#ifdef ARDUINO
#include "proto/envelope.h"
#include "sense/battery.h"
#include "sense/shtc3.h"
#include "store/nvs.h"
#include "render/usage_view.h"
#include "render/token_view.h"
#endif

namespace rcpaper {

bool layout_has_remote_image(const LayoutNode& node);
bool layout_has_any_image(const LayoutNode& node);

#ifdef ARDUINO
// ST7305 4.2" reflective panel, driven landscape 400x300 (1bpp). The dashboard
// is a fixed hand-drawn screen; the Axiom page-deck reuses the layout engine
// with a right sidebar (battery/runtime mode/nav) like the AMOLED target.
static constexpr int16_t SCREEN_W = 400;
static constexpr int16_t SCREEN_H = 300;
static constexpr int16_t SIDEBAR_W = 56;
static constexpr int16_t CONTENT_W = SCREEN_W - SIDEBAR_W;  // 344

void display_init(PanelStartup startup);
void display_prepare_for_deep_sleep();
void clear_screen();

// Hand-drawn consumer dashboard: top bar (title + clock + date), big indoor
// temperature hero, humidity + bar, bottom battery strip. `epoch` is local
// wall-clock seconds (0 => clock shows --:--).
void render_dashboard(const Shtc3Reading& env, const BatteryReading& batt,
                      uint64_t epoch, RuntimeMode runtime_mode);

bool parse_page_layout(const PageRecord& rec, LayoutNode& root);
bool render_page(const PageRecord& rec, const LayoutNode& root, bool stale,
                 const DeviceState& state, const BatteryReading& batt,
                 RuntimeMode runtime_mode);
void render_error(const char* msg, RuntimeMode runtime_mode);

// One frame of the hand-drawn weekly-allowance "road" view. A default frame is
// the static image (ECO, retained through deep sleep); HOST animation passes
// `moving` with a millisecond clock, and `sweep` < 1 while a fresh sync rolls
// the numbers and roads in from zero.
struct UsageFrame {
  bool moving = false;
  uint32_t tick = 0;   // animation frame counter: drives stepped motion (bob, puffs, lanes)
  uint32_t t_ms = 0;   // wall time: drives smooth motion (Zz float)
  float sweep = 1.0f;
};
void render_usage(const UsageView& view, const BatteryReading& batt,
                  RuntimeMode runtime_mode, const UsageFrame& frame);

// Token animates only its 0.9 s entry/sync sweep; there is no idle motion.
struct TokenFrame {
  float sweep = 1.0f;
};
void render_token(const TokenView& view, const BatteryReading& batt,
                  RuntimeMode runtime_mode, const TokenFrame& frame);
#endif

}  // namespace rcpaper
