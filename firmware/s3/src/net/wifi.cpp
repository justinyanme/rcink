// src/net/wifi.cpp
#include "net/wifi.h"
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "status/led_status.h"

namespace rcpaper {

bool wifi_connect(int max_attempts) {
  Serial.printf("[wifi] connecting to %s ...\n", WIFI_SSID);
  led_status_show(LedState::WifiConnecting);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < max_attempts; ++i) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[wifi] connected, IP=%s\n", WiFi.localIP().toString().c_str());
      led_status_show(LedState::WifiConnected);
      return true;
    }
    delay(500);
    led_status_tick();
    Serial.print('.');
  }
  Serial.println("\n[wifi] FAILED");
  led_status_show(LedState::WifiFailed);
  return false;
}

void wifi_disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  led_status_show(LedState::IdleDisconnected);
}

}  // namespace rcpaper
