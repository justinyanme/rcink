#include "input/button_capture.h"

#include <Arduino.h>

#include "power/wake_reason.h"

namespace rcpaper {
namespace {

constexpr uint32_t kButtonDebounceMs = 30;

volatile uint32_t g_count_a = 0;
volatile uint32_t g_count_b = 0;
volatile uint32_t g_count_c = 0;
volatile uint32_t g_last_a_ms = 0;
volatile uint32_t g_last_b_ms = 0;
volatile uint32_t g_last_c_ms = 0;

void IRAM_ATTR isr_btn_a() {
  const uint32_t now = millis();
  if (now - g_last_a_ms < kButtonDebounceMs) return;
  g_last_a_ms = now;
  g_count_a++;
}

void IRAM_ATTR isr_btn_b() {
  const uint32_t now = millis();
  if (now - g_last_b_ms < kButtonDebounceMs) return;
  g_last_b_ms = now;
  g_count_b++;
}

void IRAM_ATTR isr_btn_c() {
  const uint32_t now = millis();
  if (now - g_last_c_ms < kButtonDebounceMs) return;
  g_last_c_ms = now;
  g_count_c++;
}

}  // namespace

void install_button_isrs() {
  pinMode(kButtonAGpio, INPUT_PULLUP);
  pinMode(kButtonBGpio, INPUT_PULLUP);
  pinMode(kButtonCGpio, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kButtonAGpio), isr_btn_a, FALLING);
  attachInterrupt(digitalPinToInterrupt(kButtonBGpio), isr_btn_b, FALLING);
  attachInterrupt(digitalPinToInterrupt(kButtonCGpio), isr_btn_c, FALLING);
}

uint64_t sample_button_low_mask() {
  uint64_t mask = 0;
  if (digitalRead(kButtonAGpio) == LOW) mask |= kButtonAWakeMask;
  if (digitalRead(kButtonBGpio) == LOW) mask |= kButtonBWakeMask;
  if (digitalRead(kButtonCGpio) == LOW) mask |= kButtonCWakeMask;
  return mask;
}

ButtonPressCounts button_press_counts() {
  ButtonPressCounts counts;
  counts.a = g_count_a;
  counts.b = g_count_b;
  counts.c = g_count_c;
  return counts;
}

}  // namespace rcpaper
