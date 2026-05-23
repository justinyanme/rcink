#include "status/led_status.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <esp32-hal-rmt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace rcpaper {
namespace {

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct Pattern {
  Rgb a;
  Rgb b;
  uint16_t period_ms;
  uint16_t on_ms;
};

bool g_ready = false;
bool g_task_started = false;
bool g_warned_unavailable = false;
LedState g_state = LedState::Off;
uint32_t g_state_start_ms = 0;
Rgb      g_overlay_rgb{0, 0, 0};
uint32_t g_overlay_until_ms = 0;
rmt_obj_t* g_rmt = nullptr;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

constexpr int kRgbLedPin = 21;
constexpr size_t kRgbLedCount = 2;

Pattern pattern_for(LedState state) {
  switch (state) {
    case LedState::Off:              return {{0, 0, 0},   {0, 0, 0},   0,    0};
    case LedState::Launch:           return {{36, 36, 36}, {36, 36, 36}, 1600, 70};
    case LedState::WifiConnecting:   return {{0, 0, 60},   {0, 0, 60},   700,  80};
    case LedState::WifiConnected:    return {{0, 56, 0},   {0, 56, 0},   1200, 70};
    case LedState::WifiFailed:       return {{72, 0, 0},   {72, 0, 0},   900,  120};
    case LedState::Syncing:          return {{0, 48, 48},  {0, 24, 72},  700,  80};
    case LedState::SyncFailed:       return {{72, 32, 0},  {72, 0, 0},   900,  120};
    case LedState::Refreshing:       return {{72, 52, 0},  {72, 52, 0},  1200, 80};
    case LedState::Idle:             return {{0, 0, 0},   {0, 0, 0},   0, 0};
    case LedState::IdleDisconnected: return {{0, 0, 0},   {0, 0, 0},   0, 0};
  }
  return {{0, 0, 0}, {0, 0, 0}, 0, 0};
}

void warn_unavailable_once() {
  if (g_warned_unavailable) return;
  g_warned_unavailable = true;
  Serial.printf("[led] unavailable board=%d pin=%d\n", (int)M5.getBoard(), kRgbLedPin);
}

void write_byte(rmt_data_t* out, uint8_t value) {
  for (int bit = 7; bit >= 0; --bit) {
    if (value & (1 << bit)) {
      out->level0 = 1;
      out->duration0 = 8;
      out->level1 = 0;
      out->duration1 = 4;
    } else {
      out->level0 = 1;
      out->duration0 = 4;
      out->level1 = 0;
      out->duration1 = 8;
    }
    ++out;
  }
}

void write_pixel(rmt_data_t*& out, Rgb c) {
  // PaperColor's chain uses the standard WS2812 GRB byte order.
  write_byte(out, c.g);
  out += 8;
  write_byte(out, c.r);
  out += 8;
  write_byte(out, c.b);
  out += 8;
}

void write_pattern(const Pattern& p, bool pulse_on) {
  const Rgb a = pulse_on ? p.a : Rgb{0, 0, 0};
  const Rgb b = pulse_on ? p.b : Rgb{0, 0, 0};

  if (!g_ready || g_rmt == nullptr) {
    warn_unavailable_once();
    return;
  }

  rmt_data_t led_data[kRgbLedCount * 24];
  rmt_data_t* out = led_data;
  for (size_t i = 0; i < kRgbLedCount; ++i) {
    write_pixel(out, (i % 2 == 0) ? a : b);
  }
  if (!rmtWriteBlocking(g_rmt, led_data, kRgbLedCount * 24)) {
    Serial.println("[led] rmtWriteBlocking failed");
  }
}

void led_task(void*) {
  LedState last_state = LedState::Off;
  bool last_on = false;
  bool first = true;
  bool last_overlay_active = false;
  Rgb  last_overlay_rgb{0, 0, 0};

  for (;;) {
    LedState state;
    uint32_t started_ms;
    Rgb      overlay_rgb;
    uint32_t overlay_until;
    portENTER_CRITICAL(&g_lock);
    state         = g_state;
    started_ms    = g_state_start_ms;
    overlay_rgb   = g_overlay_rgb;
    overlay_until = g_overlay_until_ms;
    portEXIT_CRITICAL(&g_lock);

    uint32_t now = millis();
    bool overlay_active = (int32_t)(overlay_until - now) > 0;

    if (overlay_active) {
      if (first || !last_overlay_active || overlay_rgb.r != last_overlay_rgb.r ||
          overlay_rgb.g != last_overlay_rgb.g || overlay_rgb.b != last_overlay_rgb.b) {
        rmt_data_t led_data[kRgbLedCount * 24];
        rmt_data_t* out = led_data;
        for (size_t i = 0; i < kRgbLedCount; ++i) write_pixel(out, overlay_rgb);
        if (g_ready && g_rmt != nullptr) {
          rmtWriteBlocking(g_rmt, led_data, kRgbLedCount * 24);
        }
        last_overlay_active = true;
        last_overlay_rgb    = overlay_rgb;
        first               = false;
        // Force a heartbeat redraw when the overlay clears.
        last_state          = LedState::Off;
        last_on             = false;
      }
    } else {
      const Pattern p = pattern_for(state);
      bool on = false;
      if (p.period_ms > 0 && p.on_ms > 0) {
        on = (now - started_ms) % p.period_ms < p.on_ms;
      }
      if (first || last_overlay_active || state != last_state || on != last_on) {
        write_pattern(p, on);
        first               = false;
        last_state          = state;
        last_on             = on;
        last_overlay_active = false;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace

const char* led_state_name(LedState state) {
  switch (state) {
    case LedState::Off:              return "off";
    case LedState::Launch:           return "launch";
    case LedState::WifiConnecting:   return "wifi_connecting";
    case LedState::WifiConnected:    return "wifi_connected";
    case LedState::WifiFailed:       return "wifi_failed";
    case LedState::Syncing:          return "syncing";
    case LedState::SyncFailed:       return "sync_failed";
    case LedState::Refreshing:       return "refreshing";
    case LedState::Idle:             return "idle";
    case LedState::IdleDisconnected: return "idle_disconnected";
  }
  return "?";
}

void led_status_init() {
  g_rmt = rmtInit(kRgbLedPin, RMT_TX_MODE, RMT_MEM_64);
  if (g_rmt != nullptr) {
    rmtSetTick(g_rmt, 100);
    g_ready = true;
  }
  if (g_ready) {
    Serial.printf("[led] ready pin=%d count=%u\n", kRgbLedPin, (unsigned)kRgbLedCount);
    g_task_started = xTaskCreatePinnedToCore(led_task, "led_status", 2048, nullptr, 1,
                                             nullptr, APP_CPU_NUM) == pdPASS;
    if (!g_task_started) {
      Serial.println("[led] task start failed");
      write_pattern(pattern_for(LedState::Off), false);
    }
  } else {
    warn_unavailable_once();
  }
  led_status_show(LedState::Launch);
}

void led_status_show(LedState state) {
  const bool changed = state != g_state;
  portENTER_CRITICAL(&g_lock);
  g_state = state;
  g_state_start_ms = millis();
  portEXIT_CRITICAL(&g_lock);

  if (!g_task_started) {
    write_pattern(pattern_for(LedState::Off), false);
  }
  if (changed) {
    Serial.printf("[led] state=%s\n", led_state_name(state));
  }
}

void led_status_tick() {}

void led_status_pulse(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms) {
  if (duration_ms == 0) return;
  Rgb color{r, g, b};
  uint32_t until = millis() + duration_ms;
  portENTER_CRITICAL(&g_lock);
  g_overlay_rgb      = color;
  g_overlay_until_ms = until;
  portEXIT_CRITICAL(&g_lock);

  // Push the color immediately so the user sees the flash even if the
  // led_task is mid-vTaskDelay. The task takes over for the remainder of
  // the overlay window.
  if (g_ready && g_rmt != nullptr) {
    rmt_data_t led_data[kRgbLedCount * 24];
    rmt_data_t* out = led_data;
    for (size_t i = 0; i < kRgbLedCount; ++i) write_pixel(out, color);
    rmtWriteBlocking(g_rmt, led_data, kRgbLedCount * 24);
  }
}

}  // namespace rcpaper
