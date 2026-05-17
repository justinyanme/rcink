#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include "proto/parser.h"
#include "render/engine.h"

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power  = true;
  M5.begin(cfg);
  M5.Display.setEpdMode(epd_mode_t::epd_text);
  M5.Display.fillScreen(0xFFFFFF);

  static const char* PAGE = R"({
    "type":"vstack","pad":14,"gap":6,
    "children":[
      {"type":"text","value":"rcpapercolor","size":"h1","color":"white","bg":"red","pad":8,"align":"center"},
      {"type":"divider","color":"black","thickness":2},
      {"type":"text","value":"text + divider live","size":"body"}
    ]
  })";

  JsonDocument doc;
  deserializeJson(doc, PAGE);
  rcpaper::LayoutNode root = rcpaper::parse_layout(doc.as<JsonVariantConst>());

  rcpaper::M5GFXMeasurer m(M5.Display);
  auto laid = rcpaper::run_layout(root,
                                   rcpaper::Rect{0, 0,
                                                 (int16_t)M5.Display.width(),
                                                 (int16_t)M5.Display.height()},
                                   m);
  rcpaper::paint_layout(M5.Display, laid);
  M5.Display.display();
}

void loop() { M5.update(); delay(50); }
