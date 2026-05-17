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
    {"type":"hstack","align":"center","gap":8,"children":[
      {"type":"icon","name":"claude","size":24,"color":"red"},
      {"type":"text","value":"rcpapercolor","size":"h1","color":"white","bg":"red","pad":8}
    ]},
    {"type":"divider","color":"black","thickness":2},
    {"type":"text","value":"5H WINDOW","size":"sm","weight":"bold"},
    {"type":"hbar","pct":42,"color":"blue","height":24,"label_right":"42%"},
    {"type":"text","value":"7D WINDOW","size":"sm","weight":"bold"},
    {"type":"hbar","pct":68,"color":"green","height":24,"label_right":"68%"},
    {"type":"text","value":"daily usage","size":"sm","weight":"bold"},
    {"type":"sparkline","values":[12,18,14,22,19,28,31],"color":"blue","height":50}
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
