#include "dev/console_dump.h"

#include <Arduino.h>

#include "display/st7305.h"
#include "render/page.h"

namespace rcpaper {

void console_dump_state(View view, RuntimeMode runtime_mode,
                        bool usb_host_present, const DeviceState& state,
                        const Shtc3Reading& environment,
                        const BatteryReading& battery) {
  Serial.printf(
      "[state] view=%s runtime=%s host=%d pages=%u idx=%u "
      "T=%.1f RH=%.0f env_ok=%d dark=%d last_fetch=%llu heap=%u "
      "psram=%u batt=%u%%/%umV\n",
      view == View::Dashboard ? "dashboard" : "deck",
      runtime_mode_name(runtime_mode), (int)usb_host_present,
      (unsigned)state.pages.size(), (unsigned)state.page_index,
      environment.temp_c, environment.rh, (int)environment.ok,
      (int)st7305_dark_mode(), (unsigned long long)state.last_fetch_s,
      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
      (unsigned)battery.pct, (unsigned)battery.mv);
}

void console_dump_frame(View view, const DeviceState& state) {
  size_t len = 0;
  const uint8_t* frame = st7305_last_frame(&len);
  const char* view_name =
      view == View::Dashboard ? "dashboard" : "deck";
  const char* id = "dashboard";
  if (view == View::Deck && state.page_index < state.pages.size()) {
    id = state.pages[state.page_index].id.c_str();
  }
  Serial.printf(
      "[frame] begin w=%d h=%d bytes=%u view=%s idx=%u count=%u id=%s "
      "polarity=%s\n",
      SCREEN_W, SCREEN_H, (unsigned)len, view_name,
      (unsigned)state.page_index, (unsigned)state.pages.size(), id,
      st7305_dark_mode() ? "dark" : "light");
  constexpr size_t kBytesPerLine = 32;
  for (size_t i = 0; i < len; i += kBytesPerLine) {
    const size_t end =
        i + kBytesPerLine < len ? i + kBytesPerLine : len;
    for (size_t j = i; j < end; ++j) {
      if (frame[j] < 0x10) Serial.print('0');
      Serial.print(frame[j], HEX);
    }
    Serial.println();
    delay(1);
  }
  Serial.println("[frame] end");
  Serial.flush();
}

}  // namespace rcpaper
