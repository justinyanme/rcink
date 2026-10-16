#include "demo.h"

#include <Arduino.h>

#include <cstring>

#include "frame.h"
#include "gallery.h"
#include "image.h"
#include "modes.h"

namespace rclab::demo {
namespace {

#include "own_waveforms.inc"

using gallery::Drive;

constexpr int kButtonA = 9, kButtonB = 10, kButtonC = 1;  // M5Unified PaperColor mapping
constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kHoldMs = 800;  // C held this long: native clear

uint8_t* g_plane = nullptr;
int g_page = -1;  // -1: nothing opened since boot
unsigned g_step = 0;

struct Button {
  int pin;
  bool high = true;
  uint32_t changed_ms = 0;
  bool held = false;  // hold already handled (C)
};
Button g_buttons[] = {{kButtonA}, {kButtonB}, {kButtonC}};

// tools/own_mono.py / own_color.py / own_grid.py KNOBS.
Knobs lut_knobs(uint8_t psr0, unsigned bits) {
  Knobs k;
  k.setup_tickey = true;
  k.psr[0] = psr0;
  k.psr[1] = 0xA9;
  k.booster = false;
  k.window = true;
  k.pack.bits = uint8_t(bits);
  return k;
}

void set_map(Knobs& k, const char* digits) {
  k.mapped = true;
  for (size_t i = 0; i < 8; ++i) k.map[i] = uint8_t(digits[i] - '0');
}

epd::Result custom(const uint8_t* table, const Knobs& knobs, Reply& ok) {
  modes::lut_set(0, table, kLutBytes799);
  return modes::run(Mode::Custom, knobs, ok);
}

epd::Result drive(Drive d, Reply& ok) {
  switch (d) {
    case Drive::CleanMono: {
      Knobs k = lut_knobs(0x0F, 2);
      set_map(k, "10111111");
      return custom(own_waveform_mono_clean, k, ok);
    }
    case Drive::FastMono: return custom(own_waveform_mono_fast, lut_knobs(0x0F, 2), ok);
    case Drive::Color: {
      Knobs k = lut_knobs(0x1F, 4);
      set_map(k, "10231561");
      return custom(own_waveform_color, k, ok);
    }
    case Drive::Grid: {
      Knobs k = lut_knobs(0x1F, 4);
      k.field_bits = 3;
      k.field_shift = 0;
      Reply first(true);
      if (auto r = custom(own_waveform_grid_base, k, first); !r.ok()) return r;
      k.field_shift = 3;
      return custom(own_waveform_grid_modifier, k, ok);
    }
    case Drive::Native: return modes::run(Mode::Native, Knobs{}, ok);
  }
  return {"drive"};
}

epd::Result show(unsigned page, unsigned step, bool action, Reply& ok) {
  const gallery::Page& p = gallery::kPages[page];
  const Drive d = action ? p.action : p.enter;
  p.render(g_plane, step);
  g_page = int(page);
  g_step = step;
  ok.field("page", long(page)).field("name", p.name).field("step", long(step)).field("drive", gallery::drive_name(d));
  return drive(d, ok);
}

}  // namespace

void begin(uint8_t* plane) {
  g_plane = plane;
  for (auto& b : g_buttons) {
    pinMode(b.pin, INPUT_PULLUP);
    b.high = digitalRead(b.pin) != 0;
  }
}

epd::Result open(unsigned page, Reply& ok) { return show(page, 0, false, ok); }

epd::Result act(Reply& ok) {
  if (g_page < 0) return {"no-page"};
  return show(unsigned(g_page), g_step + 1, true, ok);
}

epd::Result clear(Reply& ok) {
  render_fill(g_plane, InkWhite);
  ok.field("clear", "native");
  return modes::run(Mode::Native, Knobs{}, ok);
}

bool poll() {
  enum { None, Next, Prev, Act, Clear } event = None;
  const uint32_t now = millis();
  for (size_t i = 0; i < 3; ++i) {
    Button& b = g_buttons[i];
    const bool high = digitalRead(b.pin) != 0;
    if (high != b.high && now - b.changed_ms >= kDebounceMs) {
      b.high = high;
      b.changed_ms = now;
      const bool was_held = b.held;
      b.held = false;
      if (event != None) continue;
      if (!high && i == 0) event = Next;
      else if (!high && i == 1) event = Prev;
      else if (high && i == 2 && !was_held) event = Act;  // C acts on release: a hold is the native clear
    } else if (i == 2 && !b.high && !b.held && now - b.changed_ms >= kHoldMs && event == None) {
      b.held = true;
      event = Clear;
    }
  }
  if (event == None) return false;
  const uint32_t began = millis();
  Reply ok(true);
  epd::Result r;
  constexpr int n = int(gallery::kPageCount);
  switch (event) {
    case Clear: r = clear(ok); break;
    case Act: r = g_page >= 0 ? act(ok) : open(0, ok); break;
    case Prev: r = open(unsigned(g_page < 0 ? n - 1 : (g_page + n - 1) % n), ok); break;
    default: r = open(unsigned(g_page < 0 ? 0 : (g_page + 1) % n), ok); break;
  }
  if (r.ok()) Serial.printf("# demo %s total_ms=%lu\n", ok.c_str(), (unsigned long)(millis() - began));
  else Serial.printf("# demo FAIL stage=%s busy=%d ms=%u\n", r.stage, r.busy, unsigned(r.ms));
  // Presses during the multi-second drive are dropped: resample the levels; a C still
  // down counts as held so its release does not act.
  for (auto& b : g_buttons) b.high = digitalRead(b.pin) != 0, b.changed_ms = millis(), b.held = !b.high;
  return true;
}

}  // namespace rclab::demo
