#include "modes.h"

#include <Arduino.h>

#include <cstring>

#include "epd_init.h"
#include "frame.h"
#include "waveforms.h"

namespace rclab::modes {
namespace {

const uint8_t* g_inks = nullptr;
uint8_t* g_packed = nullptr;
uint8_t* g_old = nullptr;
bool g_old_valid = false;
uint8_t g_lut[TICKEY_WAVEFORM_BYTES];  // Mode::Custom LUT, edited by the console

const uint8_t* const kTables[] = {
    tickey_waveform_clear_initial,       tickey_waveform_mono,                tickey_waveform_mono_clear,
    tickey_waveform_color_fast_phase1,   tickey_waveform_color_fast_phase2,   tickey_waveform_color_fast_cleanup,
    tickey_waveform_color_quality_phase1, tickey_waveform_color_quality_phase2, tickey_waveform_color_quality_phase3,
};
static_assert(sizeof(kTables) / sizeof(kTables[0]) == kLutTableCount, "kLutTables order");
static_assert(TICKEY_WAVEFORM_BYTES == kLutBytes799, "LUT size");

// sf32rc display.c:50-58 configure_trial(): TICKEY 2.1.0 volatile setup.
constexpr uint8_t kTickeySetup[][2] = {{0x30, 0x04}, {0x41, 0x00}, {0xE0, 0x02}, {0xE6, 0x19}};
// FreeInk Ed2208M5Driver.cpp:196-209: partial window covering the whole panel.
constexpr uint8_t kFullWindow[] = {0x00, 0x00, 0x01, 0x8F, 0x00, 0x00, 0x02, 0x57, 0x01};

#define TRY(expr)                    \
  if (auto r_ = (expr); !r_.ok()) { \
    return r_;                       \
  }

// --- native: Panel_ED2208::_turn_on_display ---

epd::Result native(Reply& ok) {
  encode_native_frame(g_inks, g_packed);
  TRY(epd::upload_frame(g_packed, kNativeFrameBytes));
  TRY(epd::power_on());
  delay(200);
  epd::booster();
  delay(200);
  uint32_t busy_ms = 0;
  TRY(epd::refresh("drive", busy_ms));
  delay(200);
  TRY(epd::power_off());
  delay(200);
  ok.field("busy_ms", long(busy_ms));
  // The panel now shows exactly g_inks: that is a known mono history.
  mono_plane(g_inks, kPixels, g_old);
  g_old_valid = true;
  return {};
}

// --- interrupt: FreeInk e91e34e Ed2208M5Driver.cpp refresh()/interruptRefresh() ---

epd::Result interrupt(const Knobs& knobs, Reply& ok) {
  const uint8_t cdi = knobs.cdi;
  epd::write(kCmdCdi, &cdi, 1);  // FreeInk init list carries CDI 0F
  encode_native_frame(g_inks, g_packed);
  TRY(epd::upload_frame(g_packed, kNativeFrameBytes));
  TRY(epd::power_on());
  delay(20);  // FreeInk waitBusy BUSY_SETTLE_MS
  epd::booster();
  epd::write(kCmdWindow, kFullWindow, sizeof(kFullWindow));
  epd::write(kCmdCdi, &cdi, 1);
  uint32_t busy_ms = 0;
  TRY(epd::refresh_cut(knobs.cut_ms, busy_ms));
  TRY(epd::init_native());
  ok.field("cut_ms", long(knobs.cut_ms)).hex("cdi", &cdi, 1).field("busy_ms", long(busy_ms));
  return {};
}

// --- TICKEY LUT modes: sf32rc tickey_mono.c / tickey_color.c ---

epd::Result lut(const Knobs& knobs, const uint8_t* table) {
  return epd::upload_lut(knobs.psr, table, TICKEY_WAVEFORM_BYTES);
}

size_t row_bytes(Pack pack) { return kPanelWidth * pack.bits / 8; }
size_t plane_bytes(Pack pack) { return row_bytes(pack) * kPanelHeight * (pack.rows == Rows::Once ? 1 : 2); }

// g_packed holds kPanelHeight encoded rows. For two-row layouts spread them
// in place back to front (row y lands at 2y, never before it is read), adding
// the copy (Twice) or a code-0 filler row (Even/Odd); then upload.
epd::Result upload_plane(Pack pack) {
  if (pack.rows != Rows::Once) {
    const size_t rb = row_bytes(pack);
    for (size_t y = kPanelHeight; y-- > 0;) {
      uint8_t* first = g_packed + 2 * y * rb;
      uint8_t* second = first + rb;
      memmove(first, g_packed + y * rb, rb);
      switch (pack.rows) {
        case Rows::Twice: memcpy(second, first, rb); break;
        case Rows::Even: memset(second, 0, rb); break;
        case Rows::Odd: memcpy(second, first, rb), memset(first, 0, rb); break;
        case Rows::Once: break;
      }
    }
  }
  return epd::upload_frame(g_packed, plane_bytes(pack));
}

// sf32rc color_power(1): 04 + 10 ms; booster=native adds _turn_on_display's
// 200 ms / booster / 200 ms instead.
epd::Result power(const Knobs& knobs) {
  TRY(epd::power_on());
  if (knobs.booster) {
    delay(200);
    epd::booster();
    delay(200);
  } else {
    delay(10);
  }
  return {};
}

void prologue(const Knobs& knobs) {
  for (const auto& pair : kTickeySetup) {
    if (knobs.setup_tickey) epd::write(pair[0], pair + 1, 1);
  }
  // tres=200: resolution 200x600 instead of the pinned 400x600 (01 90 02 58).
  static constexpr uint8_t kHalfWidth[] = {0x00, 0xC8, 0x02, 0x58};
  if (knobs.tres_half) epd::write(kCmdResolution, kHalfWidth, sizeof(kHalfWidth));
  if (knobs.window) epd::write(kCmdWindow, kFullWindow, sizeof(kFullWindow));
}

// Cold prepare (tickey_mono.c:88-96): clear_initial over an all-zero frame,
// power retained afterwards.
epd::Result cold_clear(const Knobs& knobs, uint32_t& clear_ms) {
  TRY(lut(knobs, tickey_waveform_clear_initial));
  memset(g_packed, 0, plane_bytes(knobs.pack));
  TRY(epd::upload_frame(g_packed, plane_bytes(knobs.pack)));
  TRY(power(knobs));
  return epd::refresh("clear", clear_ms);
}

epd::Result power_down() {
  TRY(epd::power_off());
  delay(20);  // sf32rc color_power(0)
  return {};
}

epd::Result mono(Mode mode, const Knobs& knobs, Reply& ok) {
  const bool cold = !g_old_valid;
  uint32_t clear_ms = 0, busy_ms = 0;
  if (cold) TRY(cold_clear(knobs, clear_ms));
  TRY(lut(knobs, mode == Mode::MonoClear ? tickey_waveform_mono_clear : tickey_waveform_mono));
  const uint8_t* old = cold ? nullptr : g_old;
  if (knobs.pack.bits == 2) encode_mono_2bpp(g_inks, old, kPixels, g_packed);
  else encode_mono(g_inks, old, kPixels, g_packed);
  TRY(upload_plane(knobs.pack));
  if (!cold) TRY(power(knobs));  // cold path kept power on (tickey_mono.c:130-133)
  TRY(epd::refresh("mono", busy_ms));
  TRY(power_down());
  mono_plane(g_inks, kPixels, g_old);
  g_old_valid = true;
  ok.field("cold", long(cold)).field("clear_ms", long(clear_ms)).field("busy_ms", long(busy_ms));
  return {};
}

// Custom: RAM LUT over the mono transition frame. No cold clear: a missing
// history is all-zero (dark); the light plane is kept as history afterwards.
epd::Result custom(const Knobs& knobs, Reply& ok) {
  uint32_t busy_ms = 0;
  TRY(lut(knobs, g_lut));
  const uint8_t* old = g_old_valid ? g_old : nullptr;
  if (knobs.pack.bits == 2) encode_mono_2bpp(g_inks, old, kPixels, g_packed);
  else encode_mono(g_inks, old, kPixels, g_packed);
  TRY(upload_plane(knobs.pack));
  TRY(power(knobs));
  TRY(epd::refresh("custom", busy_ms));
  TRY(power_down());
  ok.field("history", long(old != nullptr)).field("busy_ms", long(busy_ms));
  mono_plane(g_inks, kPixels, g_old);
  g_old_valid = true;
  return {};
}

epd::Result staged(Mode mode, const Knobs& knobs, Reply& ok) {
  const bool fast = mode == Mode::Fast;
  const Palette palette = fast ? Palette::Fast : Palette::Quality;
  uint32_t clear_ms = 0, p1_ms = 0, p2_ms = 0, final_ms = 0;
  // Always cold: nothing here leaves sf32rc's "mono clear known" state.
  TRY(cold_clear(knobs, clear_ms));
  TRY(lut(knobs, fast ? tickey_waveform_color_fast_phase1 : tickey_waveform_color_quality_phase1));
  encode_staged(g_inks, kPixels, palette, true, g_packed);
  TRY(upload_plane(knobs.pack));
  TRY(epd::refresh("phase1", p1_ms));
  if (!fast) {  // quality phase 2 reuses the resident high plane
    TRY(lut(knobs, tickey_waveform_color_quality_phase2));
    TRY(epd::refresh("phase2", p2_ms));
  }
  TRY(lut(knobs, fast ? tickey_waveform_color_fast_phase2 : tickey_waveform_color_quality_phase3));
  encode_staged(g_inks, kPixels, palette, false, g_packed);
  TRY(upload_plane(knobs.pack));
  TRY(epd::refresh("final", final_ms));
  TRY(power_down());
  if (fast) {  // post-seed: high plane resident, no refresh (tickey_color.c:102-108)
    encode_staged(g_inks, kPixels, palette, true, g_packed);
    TRY(upload_plane(knobs.pack));
  }
  ok.field("clear_ms", long(clear_ms)).field("p1_ms", long(p1_ms));
  if (!fast) ok.field("p2_ms", long(p2_ms));
  ok.field("final_ms", long(final_ms)).field("busy_ms", long(p1_ms + p2_ms + final_ms));
  return {};
}

void knob_fields(const Knobs& knobs, Reply& ok) {
  ok.field("setup", knobs.setup_tickey ? "tickey" : "native")
      .hex("psr", knobs.psr, 2)
      .field("booster", knobs.booster ? "native" : "none")
      .field("tres", knobs.tres_half ? 200L : 400L)
      .field("win", long(knobs.window))
      .field("pack", pack_name(knobs.pack));
}

#undef TRY

}  // namespace

void begin(const uint8_t* inks, uint8_t* packed, uint8_t* old_plane) {
  g_inks = inks;
  g_packed = packed;
  g_old = old_plane;
  g_old_valid = false;
  lut_load(1);  // mono
}

void forget_history() { g_old_valid = false; }

void lut_load(size_t table) { memcpy(g_lut, kTables[table], TICKEY_WAVEFORM_BYTES); }
void lut_set(size_t offset, const uint8_t* bytes, size_t count) { memcpy(g_lut + offset, bytes, count); }
void lut_get(size_t offset, uint8_t* out, size_t count) { memcpy(out, g_lut + offset, count); }

epd::Result run(Mode mode, const Knobs& knobs, Reply& ok) {
  if (mode == Mode::Native) {
    forget_history();
    return native(ok);
  }
  if (mode == Mode::Interrupt) {
    forget_history();
    auto r = interrupt(knobs, ok);
    if (!r.ok()) {
      epd::hard_reset();
      epd::init_native();
    }
    return r;
  }
  // LUT modes: start from a known controller, end native whatever happens.
  epd::hard_reset();
  epd::Result r = epd::init_native();
  if (r.ok()) {
    prologue(knobs);
    knob_fields(knobs, ok);
    if (mode == Mode::Custom) r = custom(knobs, ok);
    else if (mode == Mode::Mono || mode == Mode::MonoClear) r = mono(mode, knobs, ok);
    else r = staged(mode, knobs, ok);
    if (mode == Mode::Fast || mode == Mode::Quality) forget_history();
  }
  if (!r.ok()) forget_history();
  epd::hard_reset();
  if (auto restored = epd::init_native(); r.ok() && !restored.ok()) return restored;
  return r;
}

}  // namespace rclab::modes
