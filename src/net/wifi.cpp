// src/net/wifi.cpp
#include "net/wifi.h"
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

namespace rcpaper {

bool wifi_connect(int max_attempts) {
  Serial.printf("[wifi] connecting to %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < max_attempts; ++i) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[wifi] connected, IP=%s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    delay(500);
    Serial.print('.');
  }
  Serial.println("\n[wifi] FAILED");
  return false;
}

void wifi_disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

}  // namespace rcpaper
