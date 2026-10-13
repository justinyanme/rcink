#include "power/runtime.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "dev/console.h"
#include "net/wifi.h"
#include "power/refresh_schedule.h"
#include "power/sleep.h"
#include "power/supply.h"
#include "sense/battery.h"
#include "status/led_status.h"
#include "store/nvs.h"

namespace rcpaper {
extern DeviceState g_state;
bool refetch_now();
void render_current();

namespace {
uint32_t refresh_deadline_ms;
uint32_t power_checked_ms;

uint8_t power_source() {
  // The pinned library's enum is incorrect: use datasheet v1.9 bitmap.
  // Captured on this board: 0x05 = USB input AND battery valid, VIN=4598 mV.
  uint8_t raw = 0xff;
  const bool ok = M5.In_I2C.readRegister(0x6E, 0x04, &raw, 1, 100000);
  if (!ok || decode_supply(raw) == Supply::Unknown) {
    Serial.printf("[power] fatal: PM1 supply read_ok=%d raw=0x%02x vin_mv=%d\n",
                  ok, raw, M5.Power.getVBUSVoltage());
    Serial.flush();
    abort();
  }
  return raw & 7;
}

void sleep_on_battery() {
  if (decode_supply(power_source()) != Supply::Battery) return;
  const uint32_t now = millis();
  const uint32_t remaining_s = refresh_due_ms(now, refresh_deadline_ms)
      ? 1 : (refresh_deadline_ms - now + 999) / 1000;
  Serial.printf("[power] battery -> sleep; next fetch in %u s\n", remaining_s);
  enter_deep_sleep_until(static_cast<uint64_t>(time(nullptr)) + remaining_s);
}
}  // namespace

void schedule_runtime_refresh() {
  const auto battery = read_battery();
  const uint32_t interval = adaptive_refresh_interval_sec(
      FETCH_INTERVAL_SEC, g_state.unchanged_fetches, battery.pct, battery.ok);
  refresh_deadline_ms = schedule_next_refresh_ms(millis(), interval);
}

void print_power_state() {
  const auto source = power_source();
  const auto battery = read_battery();
  const uint32_t now = millis();
  Serial.printf("[power] flags=0x%02x (%s) vin_mv=%d battery_mv=%u uptime_ms=%u next_fetch_ms=%u\n",
                source, decode_supply(source) == Supply::Battery ? "battery" : "external",
                M5.Power.getVBUSVoltage(), battery.mv, now,
                refresh_due_ms(now, refresh_deadline_ms)
                    ? 0 : refresh_deadline_ms - now);
}

void runtime_begin() {
  // Cache-only boot still needs a deadline; fetch paths already armed theirs.
  if (!refresh_deadline_ms) schedule_runtime_refresh();
  print_power_state();
  sleep_on_battery();
  dev::dev_buttons_init();
  Serial.println("[console] ready; external power keeps USB awake; type h");
}

void runtime_poll() {
  // Check power before admitting commands/fetches. A running render finishes
  // before rail shutdown; never cut power during a panel transaction.
  if (millis() - power_checked_ms >= 250) {
    power_checked_ms = millis();
    sleep_on_battery();
  }
  // HWCDC's bool conversion services its USB reconnect handshake. Poll it
  // while idle so queued response tails do not wait for another OUT packet.
  // flush() while disconnected discards the queue in Arduino2.0.17, so only
  // flush after the handshake reports connected.
  if (Serial) Serial.flush();
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    dev::console_dispatch(static_cast<char>(c));
    if (c > ' ') {
      Serial.printf("[console] done %c uptime_ms=%u\n", c, millis());
    }
    sleep_on_battery();
  }
  dev::dev_buttons_poll();
  if (refresh_due_ms(millis(), refresh_deadline_ms)) {
    sleep_on_battery();
    Serial.println("[power] scheduled fetch while externally powered");
    if (refetch_now()) render_current();
    if (WiFi.status() == WL_CONNECTED) wifi_disconnect();
    led_status_show(LedState::IdleDisconnected);
  }
  delay(20);
}
}  // namespace rcpaper
