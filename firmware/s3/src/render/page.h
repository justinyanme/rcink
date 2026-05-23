#pragma once
#include "proto/schema.h"

#ifdef ARDUINO
#include <M5GFX.h>
#include "net/axiom.h"
#include "sense/battery.h"
#include "sense/env.h"
#include "store/nvs.h"
#endif

namespace rcpaper {

// Pure tree predicates — reusable by the orchestrator (e.g. to decide
// whether wifi must come up for a render-only wake). Native-safe.
bool layout_has_remote_image(const LayoutNode& node);
bool layout_has_any_image(const LayoutNode& node);

#ifdef ARDUINO
// Parse a page record's stringified layout_json into a LayoutNode tree.
// Returns false on parse failure; `root` is then untouched.
bool parse_page_layout(const PageRecord& rec, LayoutNode& root);

// Top-to-bottom render of one page: status bar + page layout + nav strip,
// plus an optional "STALE" pill in the bottom-right. All draws go through
// an offscreen canvas; the panel commits once via Display.display().
void render_page(const PageRecord& rec, const LayoutNode& root, bool stale,
                 const DeviceState& state,
                 const BatteryReading& batt, const EnvReading& env);

// Fallback page for unrecoverable orchestrator states (no pages, parse fail).
void render_error(const char* msg);

// Debug helper: name for an M5GFX EPD mode (quality/text/fast/fastest).
const char* epd_mode_name(epd_mode_t m);
#endif

}  // namespace rcpaper
