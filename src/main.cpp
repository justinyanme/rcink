// src/main.cpp
#include <Arduino.h>
#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power  = true;
  M5.begin(cfg);

  M5.Display.setEpdMode(epd_mode_t::epd_text);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(20, 20);
  M5.Display.printf("rcpapercolor v0.0\n\nhello, PaperColor");
}

void loop() {
  M5.update();
  delay(50);
}
