# rcpapercolor Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ESP32-S3 firmware for the M5Stack PaperColor that wakes every 15 min (or earlier on button), queries Axiom via APL, parses the returned layout-tree JSON, renders the current page on the Spectra 6 panel, then deep-sleeps until the next tick.

**Architecture:** PlatformIO + Arduino + M5Unified/M5GFX/M5PM1. Code is split into `net/`, `proto/`, `render/`, `store/`, `power/` modules, each with a single responsibility. Pure-logic modules (palette, parser, layout engine) are unit-tested on the host via `pio test -e native`; device-bound modules (rendering, WiFi, NVS, sleep) are verified on hardware with explicit acceptance checks.

**Tech Stack:** PlatformIO `espressif32 @ 6.12.0`, board `esp32s3box`, libs `M5Unified`, `M5GFX`, `M5PM1`, `ArduinoJson@^7.4`, Unity (built-in to PlatformIO) for tests.

**Spec reference:** `docs/superpowers/specs/2026-05-17-rcpapercolor-design.md` (especially "Firmware" and "Protocol" sections).

---

## File Structure

```
platformio.ini
src/
├── main.cpp                       # wake loop + dispatch
├── config.h.example               # committed; real config.h is gitignored
├── net/
│   ├── wifi.h
│   ├── wifi.cpp                   # connectWiFi() with retry
│   ├── axiom.h
│   └── axiom.cpp                  # POST /_apl, parse matches[*].data
├── proto/
│   ├── schema.h                   # LayoutNode + enums (pure, no Arduino.h)
│   ├── parser.h
│   └── parser.cpp                 # ArduinoJson JsonVariant -> LayoutNode tree
├── render/
│   ├── palette.h
│   ├── palette.cpp                # token string -> Color enum -> M5GFX value
│   ├── fonts_icons.h
│   ├── fonts_icons.cpp            # size + weight -> font, icon name -> bitmap
│   ├── engine.h
│   ├── engine.cpp                 # layout pass (pure) + paint pass (M5GFX)
│   └── primitives.cpp             # one draw function per content primitive
├── store/
│   ├── nvs.h
│   └── nvs.cpp                    # Preferences-backed cached pages + state
└── power/
    ├── sleep.h
    └── sleep.cpp                  # RTC alarm + GPIO ext1 wake configuration
test/
├── test_native_palette/test_main.cpp
├── test_native_parser/test_main.cpp
└── test_native_engine/test_main.cpp
```

Pure modules (no `Arduino.h`, no M5GFX): `proto/schema.h`, `proto/parser.*`, `render/palette.*`, `render/engine.cpp`'s layout pass.

---

## Task 1: PlatformIO bootstrap + Hello PaperColor

**Files:**
- Create: `platformio.ini`
- Create: `src/main.cpp`
- Create: `src/config.h.example`
- Create: `boards/.gitkeep` (in case we add custom board JSON later)

- [ ] **Step 1: Write `platformio.ini`**

```ini
[platformio]
default_envs = m5stack-papercolor

; -----------------------------------------------------------------------------
; Device build
; -----------------------------------------------------------------------------
[env:m5stack-papercolor]
platform = espressif32 @ 6.12.0
board = esp32s3box
framework = arduino
board_build.partitions = default_16MB.csv
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
board_build.arduino.memory_type = qio_opi
monitor_speed = 115200
monitor_filters = default, esp32_exception_decoder
build_flags =
    -DESP32S3
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Unified = https://github.com/m5stack/M5Unified
    M5GFX = https://github.com/m5stack/M5GFX
    M5PM1 = https://github.com/m5stack/M5PM1
    bblanchon/ArduinoJson@^7.4.2

; -----------------------------------------------------------------------------
; Native tests for pure-logic modules (palette, parser, layout engine)
; -----------------------------------------------------------------------------
[env:native]
platform = native
build_flags =
    -std=gnu++17
    -DUNIT_TEST
    -Isrc
lib_deps =
    bblanchon/ArduinoJson@^7.4.2
test_build_src = no
```

- [ ] **Step 2: Create `src/config.h.example`**

```cpp
// src/config.h.example
// Copy to src/config.h and fill in your values. config.h is gitignored.
#pragma once

// WiFi
#define WIFI_SSID            "your-ssid"
#define WIFI_PASS            "your-pass"

// Axiom
#define AXIOM_QUERY_URL      "https://api.axiom.co/v1/datasets/_apl?format=legacy"
#define AXIOM_TOKEN          "xaat-your-token"
#define AXIOM_APL_QUERY      "['rcpapercolor'] | where ['kind'] == 'page' and _time > ago(1h) | summarize arg_max(_time, *) by ['id'] | project ['id'], ['order'], ['layout'] | order by ['order'] asc nulls last, ['id'] asc"

// Behavior
#define FETCH_INTERVAL_SEC   900   // 15 min
#define ROTATE_INTERVAL_SEC  120   //  2 min

// TLS root CA for api.axiom.co (ISRG Root X1 — Let's Encrypt chain).
// Paste the PEM here; mirrors the eInkClaude pattern.
#define AXIOM_ROOT_CA R"(-----BEGIN CERTIFICATE-----
...paste the PEM here...
-----END CERTIFICATE-----
)"
```

- [ ] **Step 3: Write minimal `src/main.cpp`**

```cpp
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
```

- [ ] **Step 4: Build and flash**

Run:
```bash
cp src/config.h.example src/config.h     # fill in your WiFi + Axiom values
pio run -e m5stack-papercolor
pio run -e m5stack-papercolor -t upload
pio device monitor
```

Expected: device boots, EPD shows "rcpapercolor v0.0 / hello, PaperColor". Serial monitor shows boot logs at 115200.

- [ ] **Step 5: Build the native test environment (no tests yet)**

Run: `pio test -e native --without-uploading --without-testing` (or `pio run -e native`) just to confirm the toolchain works.

Expected: no errors. (Empty test discovery is OK.)

- [ ] **Step 6: Commit**

```bash
git add platformio.ini src/main.cpp src/config.h.example
git commit -m "feat(fw): bootstrap PlatformIO project, Hello PaperColor on EPD"
```

---

## Task 2: Color palette — `render/palette.{h,cpp}` (native TDD)

**Files:**
- Create: `src/render/palette.h`
- Create: `src/render/palette.cpp`
- Create: `test/test_native_palette/test_main.cpp`

The palette maps the protocol's 6 named tokens to a `Color` enum, and the enum to whatever M5GFX color value the E6 panel expects. The string→enum step is pure and unit-tested; the enum→M5GFX-value step has a thin device-side lookup.

- [ ] **Step 1: Write the failing native test**

```cpp
// test/test_native_palette/test_main.cpp
#include <unity.h>
#include "render/palette.h"

using namespace rcpaper;

void test_known_tokens() {
  TEST_ASSERT_EQUAL(Color::Black,  color_from_token("black"));
  TEST_ASSERT_EQUAL(Color::White,  color_from_token("white"));
  TEST_ASSERT_EQUAL(Color::Red,    color_from_token("red"));
  TEST_ASSERT_EQUAL(Color::Yellow, color_from_token("yellow"));
  TEST_ASSERT_EQUAL(Color::Green,  color_from_token("green"));
  TEST_ASSERT_EQUAL(Color::Blue,   color_from_token("blue"));
}

void test_unknown_token_falls_back_to_black() {
  TEST_ASSERT_EQUAL(Color::Black, color_from_token("magenta"));
  TEST_ASSERT_EQUAL(Color::Black, color_from_token(nullptr));
  TEST_ASSERT_EQUAL(Color::Black, color_from_token(""));
}

void test_null_or_empty_explicit() {
  // also accept missing-color use case
  TEST_ASSERT_EQUAL(Color::Black, color_from_token("RED"));   // case-sensitive
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_known_tokens);
  RUN_TEST(test_unknown_token_falls_back_to_black);
  RUN_TEST(test_null_or_empty_explicit);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it, confirm it fails**

Run: `pio test -e native -f test_native_palette`
Expected: build failure (`palette.h` doesn't exist).

- [ ] **Step 3: Implement `palette.h`**

```cpp
// src/render/palette.h
#pragma once
#include <cstdint>

namespace rcpaper {

enum class Color : uint8_t {
  Black  = 0,
  White  = 1,
  Red    = 2,
  Yellow = 3,
  Green  = 4,
  Blue   = 5,
};

// Pure: maps protocol token to enum. Case-sensitive. Unknown → Black.
Color color_from_token(const char* token);

#ifdef ARDUINO
// Device-side: maps Color enum to the M5GFX value the E6 panel expects.
// Defined only in the Arduino build.
uint32_t color_to_m5gfx(Color c);
#endif

}  // namespace rcpaper
```

- [ ] **Step 4: Implement `palette.cpp`**

```cpp
// src/render/palette.cpp
#include "render/palette.h"
#include <cstring>

#ifdef ARDUINO
#include <M5GFX.h>
#endif

namespace rcpaper {

Color color_from_token(const char* token) {
  if (!token || !*token) return Color::Black;
  if (std::strcmp(token, "black")  == 0) return Color::Black;
  if (std::strcmp(token, "white")  == 0) return Color::White;
  if (std::strcmp(token, "red")    == 0) return Color::Red;
  if (std::strcmp(token, "yellow") == 0) return Color::Yellow;
  if (std::strcmp(token, "green")  == 0) return Color::Green;
  if (std::strcmp(token, "blue")   == 0) return Color::Blue;
  return Color::Black;
}

#ifdef ARDUINO
// M5GFX provides 24-bit RGB; the Spectra 6 panel quantises to the nearest of
// its 6 inks internally. Using canonical RGB anchors gives a stable mapping.
uint32_t color_to_m5gfx(Color c) {
  switch (c) {
    case Color::Black:  return 0x000000;
    case Color::White:  return 0xFFFFFF;
    case Color::Red:    return 0xD92027;
    case Color::Yellow: return 0xF7C815;
    case Color::Green:  return 0x1EA84B;
    case Color::Blue:   return 0x0F4D92;
  }
  return 0x000000;
}
#endif

}  // namespace rcpaper
```

- [ ] **Step 5: Run tests, confirm they pass**

Run: `pio test -e native -f test_native_palette`
Expected: all green.

- [ ] **Step 6: Commit**

```bash
git add src/render/palette.h src/render/palette.cpp test/test_native_palette/
git commit -m "feat(fw): color palette tokens + native tests"
```

---

## Task 3: Schema types + JSON parser — `proto/{schema,parser}.{h,cpp}` (native TDD)

**Files:**
- Create: `src/proto/schema.h`
- Create: `src/proto/parser.h`
- Create: `src/proto/parser.cpp`
- Create: `test/test_native_parser/test_main.cpp`

Schema is pure C++ (no Arduino.h). One struct per primitive, plus a `LayoutNode` variant-style container. Parser walks an `ArduinoJson::JsonVariantConst` and builds the tree (heap-allocated via `std::unique_ptr<LayoutNode>` for children).

- [ ] **Step 1: Write the failing parser tests**

```cpp
// test/test_native_parser/test_main.cpp
#include <unity.h>
#include <ArduinoJson.h>
#include "proto/parser.h"

using namespace rcpaper;

JsonDocument doc;

LayoutNode parse_str(const char* src) {
  doc.clear();
  deserializeJson(doc, src);
  return parse_layout(doc.as<JsonVariantConst>());
}

void test_text_minimal() {
  auto n = parse_str(R"({"type":"text","value":"CLAUDE"})");
  TEST_ASSERT_EQUAL(NodeType::Text, n.type);
  TEST_ASSERT_EQUAL_STRING("CLAUDE", n.as_text.value.c_str());
  TEST_ASSERT_EQUAL(Color::Black, n.as_text.color);   // default
  TEST_ASSERT_EQUAL(TextSize::Body, n.as_text.size);
}

void test_text_full() {
  auto n = parse_str(R"({
    "type":"text","value":"CLAUDE",
    "size":"h1","color":"white","weight":"bold",
    "align":"center","bg":"red","pad":8
  })");
  TEST_ASSERT_EQUAL(NodeType::Text, n.type);
  TEST_ASSERT_EQUAL(TextSize::H1, n.as_text.size);
  TEST_ASSERT_EQUAL(Color::White, n.as_text.color);
  TEST_ASSERT_EQUAL(Weight::Bold, n.as_text.weight);
  TEST_ASSERT_EQUAL(Align::Center, n.as_text.align);
  TEST_ASSERT_EQUAL(Color::Red, n.as_text.bg);
  TEST_ASSERT_EQUAL(8, n.as_text.pad);
  TEST_ASSERT_TRUE(n.as_text.has_bg);
}

void test_vstack_with_children() {
  auto n = parse_str(R"({
    "type":"vstack","pad":14,"gap":6,
    "children":[
      {"type":"text","value":"A"},
      {"type":"spacer","size":12},
      {"type":"divider","color":"black","thickness":2}
    ]
  })");
  TEST_ASSERT_EQUAL(NodeType::VStack, n.type);
  TEST_ASSERT_EQUAL(14, n.as_stack.pad);
  TEST_ASSERT_EQUAL(6, n.as_stack.gap);
  TEST_ASSERT_EQUAL(3, n.as_stack.children.size());
  TEST_ASSERT_EQUAL(NodeType::Text,    n.as_stack.children[0].type);
  TEST_ASSERT_EQUAL(NodeType::Spacer,  n.as_stack.children[1].type);
  TEST_ASSERT_EQUAL(NodeType::Divider, n.as_stack.children[2].type);
  TEST_ASSERT_EQUAL(12, n.as_stack.children[1].as_spacer.size);
  TEST_ASSERT_TRUE(n.as_stack.children[1].as_spacer.has_size);
}

void test_spacer_flex() {
  auto n = parse_str(R"({"type":"spacer"})");
  TEST_ASSERT_EQUAL(NodeType::Spacer, n.type);
  TEST_ASSERT_FALSE(n.as_spacer.has_size);
}

void test_hbar() {
  auto n = parse_str(R"({"type":"hbar","pct":42,"color":"blue","height":24,"label_right":"42%"})");
  TEST_ASSERT_EQUAL(NodeType::HBar, n.type);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.0f, n.as_hbar.pct);
  TEST_ASSERT_EQUAL(Color::Blue, n.as_hbar.color);
  TEST_ASSERT_EQUAL(24, n.as_hbar.height);
  TEST_ASSERT_EQUAL_STRING("42%", n.as_hbar.label_right.c_str());
}

void test_sparkline_values() {
  auto n = parse_str(R"({"type":"sparkline","values":[1,2,3,4,5],"color":"blue","height":40})");
  TEST_ASSERT_EQUAL(NodeType::Sparkline, n.type);
  TEST_ASSERT_EQUAL(5, n.as_sparkline.values.size());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, n.as_sparkline.values[2]);
}

void test_unknown_type_becomes_unknown() {
  auto n = parse_str(R"({"type":"foobar"})");
  TEST_ASSERT_EQUAL(NodeType::Unknown, n.type);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_text_minimal);
  RUN_TEST(test_text_full);
  RUN_TEST(test_vstack_with_children);
  RUN_TEST(test_spacer_flex);
  RUN_TEST(test_hbar);
  RUN_TEST(test_sparkline_values);
  RUN_TEST(test_unknown_type_becomes_unknown);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it, confirm it fails**

Run: `pio test -e native -f test_native_parser`
Expected: build failure (no schema/parser headers).

- [ ] **Step 3: Implement `schema.h`**

```cpp
// src/proto/schema.h
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "render/palette.h"

namespace rcpaper {

enum class NodeType : uint8_t {
  Unknown = 0,
  VStack, HStack, Spacer, Divider,
  Text, HBar, Icon, Image, Sparkline, Marker,
};

enum class TextSize : uint8_t { H1, H2, Body, Sm };
enum class Weight   : uint8_t { Regular, Bold };
enum class Align    : uint8_t { Start, Center, End };  // start = left/top

struct TextNode {
  std::string value;
  TextSize size   = TextSize::Body;
  Color    color  = Color::Black;
  Weight   weight = Weight::Regular;
  Align    align  = Align::Start;
  Color    bg     = Color::White;
  bool     has_bg = false;
  int16_t  pad    = 0;
  uint8_t  max_lines = 0;  // 0 = unlimited
};

struct StackNode {
  int16_t gap = 0;
  int16_t pad = 0;
  Align   align = Align::Start;
  std::vector<struct LayoutNode> children;
};

struct SpacerNode {
  int16_t size = 0;
  bool    has_size = false;  // false = flex
};

struct DividerNode {
  Color   color = Color::Black;
  int16_t thickness = 1;
};

struct HBarNode {
  float    pct = 0;
  Color    color = Color::Black;
  int16_t  height = 12;
  std::string label_left;
  std::string label_right;
};

struct IconNode {
  std::string name;
  int16_t     size = 16;
  Color       color = Color::Black;
};

struct ImageNode {
  std::string name;
  int16_t     width  = 0;
  int16_t     height = 0;
};

struct SparklineNode {
  std::vector<float> values;
  Color   color  = Color::Black;
  int16_t height = 24;
};

struct MarkerNode {
  float pct = 0;
  Color color = Color::Red;
};

// Tagged union. The variant kept simple (POD members) so the parser can build
// it with `node.type = ...; node.as_xxx = ...;`.
struct LayoutNode {
  NodeType type = NodeType::Unknown;
  // Only the field matching `type` is meaningful; others are zero-initialised.
  StackNode      as_stack;
  SpacerNode     as_spacer;
  DividerNode    as_divider;
  TextNode       as_text;
  HBarNode       as_hbar;
  IconNode       as_icon;
  ImageNode      as_image;
  SparklineNode  as_sparkline;
  MarkerNode     as_marker;
};

}  // namespace rcpaper
```

- [ ] **Step 4: Implement `parser.h`**

```cpp
// src/proto/parser.h
#pragma once
#include <ArduinoJson.h>
#include "proto/schema.h"

namespace rcpaper {

LayoutNode parse_layout(JsonVariantConst v);

}
```

- [ ] **Step 5: Implement `parser.cpp`**

```cpp
// src/proto/parser.cpp
#include "proto/parser.h"
#include <cstring>

namespace rcpaper {

static TextSize parse_size(const char* s) {
  if (!s) return TextSize::Body;
  if (std::strcmp(s, "h1")   == 0) return TextSize::H1;
  if (std::strcmp(s, "h2")   == 0) return TextSize::H2;
  if (std::strcmp(s, "body") == 0) return TextSize::Body;
  if (std::strcmp(s, "sm")   == 0) return TextSize::Sm;
  return TextSize::Body;
}

static Weight parse_weight(const char* s) {
  if (s && std::strcmp(s, "bold") == 0) return Weight::Bold;
  return Weight::Regular;
}

static Align parse_align(const char* s) {
  if (!s) return Align::Start;
  if (std::strcmp(s, "center") == 0) return Align::Center;
  if (std::strcmp(s, "right")  == 0) return Align::End;
  if (std::strcmp(s, "end")    == 0) return Align::End;
  return Align::Start;
}

static NodeType parse_type(const char* s) {
  if (!s) return NodeType::Unknown;
  if (std::strcmp(s, "vstack")    == 0) return NodeType::VStack;
  if (std::strcmp(s, "hstack")    == 0) return NodeType::HStack;
  if (std::strcmp(s, "spacer")    == 0) return NodeType::Spacer;
  if (std::strcmp(s, "divider")   == 0) return NodeType::Divider;
  if (std::strcmp(s, "text")      == 0) return NodeType::Text;
  if (std::strcmp(s, "hbar")      == 0) return NodeType::HBar;
  if (std::strcmp(s, "icon")      == 0) return NodeType::Icon;
  if (std::strcmp(s, "image")     == 0) return NodeType::Image;
  if (std::strcmp(s, "sparkline") == 0) return NodeType::Sparkline;
  if (std::strcmp(s, "marker")    == 0) return NodeType::Marker;
  return NodeType::Unknown;
}

static void fill_stack(StackNode& s, JsonVariantConst v) {
  s.gap  = v["gap"]  | 0;
  s.pad  = v["pad"]  | 0;
  s.align = parse_align(v["align"] | (const char*)nullptr);
  if (v["children"].is<JsonArrayConst>()) {
    for (JsonVariantConst c : v["children"].as<JsonArrayConst>()) {
      s.children.push_back(parse_layout(c));
    }
  }
}

LayoutNode parse_layout(JsonVariantConst v) {
  LayoutNode n;
  const char* type = v["type"] | (const char*)nullptr;
  n.type = parse_type(type);

  switch (n.type) {
    case NodeType::VStack:
    case NodeType::HStack:
      fill_stack(n.as_stack, v);
      break;

    case NodeType::Spacer:
      if (v["size"].is<int>()) {
        n.as_spacer.size = v["size"].as<int>();
        n.as_spacer.has_size = true;
      }
      break;

    case NodeType::Divider:
      n.as_divider.color     = color_from_token(v["color"] | "black");
      n.as_divider.thickness = v["thickness"] | 1;
      break;

    case NodeType::Text: {
      const char* val = v["value"] | "";
      n.as_text.value     = val;
      n.as_text.size      = parse_size(v["size"]   | (const char*)nullptr);
      n.as_text.color     = color_from_token(v["color"] | "black");
      n.as_text.weight    = parse_weight(v["weight"] | (const char*)nullptr);
      n.as_text.align     = parse_align(v["align"]  | (const char*)nullptr);
      const char* bg      = v["bg"]    | (const char*)nullptr;
      if (bg && *bg) { n.as_text.bg = color_from_token(bg); n.as_text.has_bg = true; }
      n.as_text.pad       = v["pad"] | 0;
      n.as_text.max_lines = v["max_lines"] | 0;
      break;
    }

    case NodeType::HBar:
      n.as_hbar.pct         = v["pct"]    | 0.0f;
      n.as_hbar.color       = color_from_token(v["color"] | "black");
      n.as_hbar.height      = v["height"] | 12;
      n.as_hbar.label_left  = (const char*)(v["label_left"]  | "");
      n.as_hbar.label_right = (const char*)(v["label_right"] | "");
      break;

    case NodeType::Icon:
      n.as_icon.name  = (const char*)(v["name"] | "");
      n.as_icon.size  = v["size"]  | 16;
      n.as_icon.color = color_from_token(v["color"] | "black");
      break;

    case NodeType::Image:
      n.as_image.name   = (const char*)(v["name"] | "");
      n.as_image.width  = v["width"]  | 0;
      n.as_image.height = v["height"] | 0;
      break;

    case NodeType::Sparkline:
      n.as_sparkline.color  = color_from_token(v["color"] | "black");
      n.as_sparkline.height = v["height"] | 24;
      if (v["values"].is<JsonArrayConst>()) {
        for (JsonVariantConst x : v["values"].as<JsonArrayConst>()) {
          n.as_sparkline.values.push_back(x.as<float>());
        }
      }
      break;

    case NodeType::Marker:
      n.as_marker.pct   = v["pct"] | 0.0f;
      n.as_marker.color = color_from_token(v["color"] | "red");
      break;

    case NodeType::Unknown:
    default:
      break;
  }
  return n;
}

}  // namespace rcpaper
```

- [ ] **Step 6: Run tests, confirm they pass**

Run: `pio test -e native -f test_native_parser`
Expected: all green.

- [ ] **Step 7: Commit**

```bash
git add src/proto/ test/test_native_parser/
git commit -m "feat(fw): schema + JSON parser with native tests"
```

---

## Task 4: Layout engine — `render/engine.{h,cpp}` layout pass only (native TDD)

**Files:**
- Create: `src/render/engine.h`
- Create: `src/render/engine.cpp` (layout pass only — paint pass added in later tasks)
- Create: `test/test_native_engine/test_main.cpp`

The layout pass walks the tree and assigns each node a `Rect{x,y,w,h}` given a viewport. Pure C++, no M5GFX. Sizing rules:
- `text` natural size: `w = approx_chars * font.advance`, `h = font.line_height + 2*pad`. Use a `Measurer` interface so the test can plug a deterministic stub; the device build supplies a real `M5GFXMeasurer`.
- `hbar` / `sparkline`: fill parent's cross-axis; main-axis = `height`.
- `divider`: thickness on main-axis; fills cross-axis.
- `spacer` with size: fixed; without size: flex (gets leftover space, split evenly with siblings).
- `vstack`/`hstack`: walks children with `pad` inset, `gap` between, applying `align` cross-axis. Flex spacers split remaining main-axis space evenly.

- [ ] **Step 1: Write the failing engine test**

```cpp
// test/test_native_engine/test_main.cpp
#include <unity.h>
#include "render/engine.h"
#include "proto/schema.h"

using namespace rcpaper;

// Deterministic measurer: every text is 10*chars wide, 16 tall (regardless of
// size). Icons/images use their declared size. Sparklines/hbars use parent w.
struct FakeMeasurer : public Measurer {
  Size text_size(const TextNode& t) const override {
    return Size{ (int16_t)(t.value.size() * 10 + 2 * t.pad),
                 (int16_t)(16 + 2 * t.pad) };
  }
  Size icon_size(const IconNode& i) const override { return Size{i.size, i.size}; }
  Size image_size(const ImageNode& i) const override { return Size{i.width, i.height}; }
};

void test_vstack_basic_layout() {
  LayoutNode root;
  root.type = NodeType::VStack;
  root.as_stack.pad = 10;
  root.as_stack.gap = 4;

  LayoutNode t;
  t.type = NodeType::Text;
  t.as_text.value = "AAA";  // 3 chars → w=30, h=16
  root.as_stack.children.push_back(t);
  t.as_text.value = "BBBBB";  // 5 chars → w=50, h=16
  root.as_stack.children.push_back(t);

  FakeMeasurer m;
  auto layout = run_layout(root, Rect{0, 0, 400, 600}, m);

  TEST_ASSERT_EQUAL(2, layout.children.size());
  TEST_ASSERT_EQUAL(10, layout.children[0].rect.x);
  TEST_ASSERT_EQUAL(10, layout.children[0].rect.y);
  TEST_ASSERT_EQUAL(16, layout.children[0].rect.h);

  TEST_ASSERT_EQUAL(10, layout.children[1].rect.x);
  TEST_ASSERT_EQUAL(10 + 16 + 4, layout.children[1].rect.y);  // pad + first.h + gap
  TEST_ASSERT_EQUAL(16, layout.children[1].rect.h);
}

void test_flex_spacer_eats_remaining_space() {
  LayoutNode root;
  root.type = NodeType::VStack;

  LayoutNode top;
  top.type = NodeType::Text; top.as_text.value = "X";   // h=16
  LayoutNode sp; sp.type = NodeType::Spacer;            // flex (no size)
  LayoutNode bot;
  bot.type = NodeType::Text; bot.as_text.value = "Y";   // h=16

  root.as_stack.children = {top, sp, bot};

  FakeMeasurer m;
  auto layout = run_layout(root, Rect{0, 0, 100, 200}, m);

  TEST_ASSERT_EQUAL(0, layout.children[0].rect.y);
  TEST_ASSERT_EQUAL(200 - 16, layout.children[2].rect.y);     // bottom pinned
  TEST_ASSERT_EQUAL(16, layout.children[1].rect.y);            // spacer starts after top
  TEST_ASSERT_EQUAL(200 - 16 - 16, layout.children[1].rect.h); // spacer fills middle
}

void test_align_center_in_hstack() {
  LayoutNode root;
  root.type = NodeType::HStack;
  root.as_stack.align = Align::Center;

  LayoutNode tall;
  tall.type = NodeType::Text; tall.as_text.value = "T";
  tall.as_text.pad = 10;     // h = 16 + 20 = 36
  LayoutNode short_;
  short_.type = NodeType::Text; short_.as_text.value = "S"; // h = 16

  root.as_stack.children = {tall, short_};

  FakeMeasurer m;
  auto layout = run_layout(root, Rect{0, 0, 200, 50}, m);

  // Container row height = max child = 36
  // Short child (h=16) should be centred: y = (36 - 16) / 2 = 10
  TEST_ASSERT_EQUAL(0,  layout.children[0].rect.y);
  TEST_ASSERT_EQUAL(10, layout.children[1].rect.y);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_vstack_basic_layout);
  RUN_TEST(test_flex_spacer_eats_remaining_space);
  RUN_TEST(test_align_center_in_hstack);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it, confirm it fails**

Run: `pio test -e native -f test_native_engine`
Expected: build failure (engine.h missing).

- [ ] **Step 3: Implement `engine.h`**

```cpp
// src/render/engine.h
#pragma once
#include "proto/schema.h"
#include <cstdint>
#include <vector>

namespace rcpaper {

struct Size { int16_t w = 0; int16_t h = 0; };
struct Rect { int16_t x = 0; int16_t y = 0; int16_t w = 0; int16_t h = 0; };

// Pluggable measurer (device supplies M5GFX impl; tests supply a stub).
struct Measurer {
  virtual ~Measurer() = default;
  virtual Size text_size(const TextNode&)  const = 0;
  virtual Size icon_size(const IconNode&)  const = 0;
  virtual Size image_size(const ImageNode&) const = 0;
};

struct LaidOutNode {
  const LayoutNode* node = nullptr;
  Rect              rect;
  std::vector<LaidOutNode> children;
};

LaidOutNode run_layout(const LayoutNode& root, Rect viewport, const Measurer& m);

}  // namespace rcpaper
```

- [ ] **Step 4: Implement `engine.cpp` (layout pass)**

```cpp
// src/render/engine.cpp
#include "render/engine.h"
#include <algorithm>

namespace rcpaper {

static Size natural_size(const LayoutNode& n, const Measurer& m, int16_t parent_w) {
  switch (n.type) {
    case NodeType::Text:      return m.text_size(n.as_text);
    case NodeType::Icon:      return m.icon_size(n.as_icon);
    case NodeType::Image:     return m.image_size(n.as_image);
    case NodeType::Spacer:    return Size{ n.as_spacer.has_size ? n.as_spacer.size : (int16_t)0,
                                            n.as_spacer.has_size ? n.as_spacer.size : (int16_t)0 };
    case NodeType::Divider:   return Size{ parent_w, n.as_divider.thickness };
    case NodeType::HBar:      return Size{ parent_w, n.as_hbar.height };
    case NodeType::Sparkline: return Size{ parent_w, n.as_sparkline.height };
    case NodeType::Marker:    return Size{ 1, 0 };  // no own space; overlays sibling
    case NodeType::VStack:
    case NodeType::HStack:    return Size{ parent_w, 0 };  // resolved recursively
    case NodeType::Unknown:
    default:                  return Size{ 0, 0 };
  }
}

static LaidOutNode layout_stack(const LayoutNode& root, Rect viewport, const Measurer& m);

static LaidOutNode layout_leaf(const LayoutNode& n, Rect viewport, const Measurer& m) {
  LaidOutNode out;
  out.node = &n;
  out.rect = viewport;
  return out;
}

static LaidOutNode layout_node(const LayoutNode& n, Rect viewport, const Measurer& m) {
  if (n.type == NodeType::VStack || n.type == NodeType::HStack) {
    return layout_stack(n, viewport, m);
  }
  return layout_leaf(n, viewport, m);
}

static LaidOutNode layout_stack(const LayoutNode& root, Rect viewport, const Measurer& m) {
  const bool vertical = (root.type == NodeType::VStack);
  const auto& s = root.as_stack;

  Rect inner = viewport;
  inner.x += s.pad;     inner.y += s.pad;
  inner.w -= 2 * s.pad; inner.h -= 2 * s.pad;

  // First pass: measure children that have a natural main-axis size; count flex spacers.
  std::vector<int16_t> child_main(s.children.size(), 0);
  std::vector<int16_t> child_cross(s.children.size(), 0);
  int flex_count = 0;
  int16_t fixed_main = 0;
  int16_t cross_extent = vertical ? inner.w : inner.h;

  for (size_t i = 0; i < s.children.size(); ++i) {
    const auto& c = s.children[i];
    if (c.type == NodeType::Spacer && !c.as_spacer.has_size) {
      flex_count++;
      continue;
    }
    Size sz = natural_size(c, m, cross_extent);
    if (c.type == NodeType::VStack || c.type == NodeType::HStack) {
      // Recurse with a probe viewport to measure
      Rect probe = vertical ? Rect{0, 0, cross_extent, 9999}
                            : Rect{0, 0, 9999, cross_extent};
      LaidOutNode laid = layout_stack(c, probe, m);
      // Compute used main-axis extent
      int16_t used = 0;
      for (auto& ch : laid.children) {
        int16_t end = vertical ? (int16_t)(ch.rect.y + ch.rect.h - laid.rect.y)
                               : (int16_t)(ch.rect.x + ch.rect.w - laid.rect.x);
        if (end > used) used = end;
      }
      sz = vertical ? Size{cross_extent, used} : Size{used, cross_extent};
    }
    child_main[i]  = vertical ? sz.h : sz.w;
    child_cross[i] = vertical ? sz.w : sz.h;
    fixed_main += child_main[i];
  }

  int16_t gaps_total = (s.children.size() > 1) ? (int16_t)((s.children.size() - 1) * s.gap) : 0;
  int16_t main_extent = vertical ? inner.h : inner.w;
  int16_t flex_remaining = std::max<int16_t>(0, (int16_t)(main_extent - fixed_main - gaps_total));
  int16_t per_flex = flex_count > 0 ? (int16_t)(flex_remaining / flex_count) : (int16_t)0;

  // Second pass: place children
  LaidOutNode out;
  out.node = &root;
  out.rect = viewport;
  int16_t cursor = vertical ? inner.y : inner.x;

  for (size_t i = 0; i < s.children.size(); ++i) {
    const auto& c = s.children[i];
    int16_t main_size = child_main[i];
    int16_t cross_size = child_cross[i] > 0 ? child_cross[i] : cross_extent;

    if (c.type == NodeType::Spacer && !c.as_spacer.has_size) {
      main_size = per_flex;
      cross_size = cross_extent;
    }

    // Cross alignment
    int16_t cross_origin = vertical ? inner.x : inner.y;
    int16_t cross_pos = cross_origin;
    if (s.align == Align::Center) {
      cross_pos = cross_origin + (cross_extent - cross_size) / 2;
    } else if (s.align == Align::End) {
      cross_pos = cross_origin + (cross_extent - cross_size);
    }

    Rect child_vp = vertical
      ? Rect{cross_pos, cursor, cross_size, main_size}
      : Rect{cursor, cross_pos, main_size, cross_size};

    out.children.push_back(layout_node(c, child_vp, m));

    cursor += main_size;
    if (i + 1 < s.children.size()) cursor += s.gap;
  }

  return out;
}

LaidOutNode run_layout(const LayoutNode& root, Rect viewport, const Measurer& m) {
  return layout_node(root, viewport, m);
}

}  // namespace rcpaper
```

- [ ] **Step 5: Run tests, confirm they pass**

Run: `pio test -e native -f test_native_engine`
Expected: all green.

- [ ] **Step 6: Commit**

```bash
git add src/render/engine.h src/render/engine.cpp test/test_native_engine/
git commit -m "feat(fw): flex layout engine + native tests"
```

---

## Task 5: Font + icon registry — `render/fonts_icons.{h,cpp}` (device-bound)

**Files:**
- Create: `src/render/fonts_icons.h`
- Create: `src/render/fonts_icons.cpp`

Picks a `lgfx::IFont*` for each `(TextSize, Weight)` combination, and resolves icon names to bitmap data. V1 ships placeholder icons (a filled circle stamp) so the device can render `icon` nodes without bitmap authoring; real icons land in a later iteration.

- [ ] **Step 1: Implement `fonts_icons.h`**

```cpp
// src/render/fonts_icons.h
#pragma once
#include <M5GFX.h>
#include "proto/schema.h"

namespace rcpaper {

// Returns a font pointer the M5GFX `setFont()` accepts.
const lgfx::IFont* font_for(TextSize size, Weight weight);

// Pixel height of the chosen font (M5GFX `fontHeight()`-equivalent).
int16_t font_height(TextSize size, Weight weight);

// Draws a placeholder icon: a filled square of `name`-derived size + colour.
// Returns the size drawn.
struct IconResult { int16_t w; int16_t h; };
IconResult draw_icon_placeholder(M5GFX& gfx, int16_t x, int16_t y,
                                 const IconNode& n);

}  // namespace rcpaper
```

- [ ] **Step 2: Implement `fonts_icons.cpp`**

```cpp
// src/render/fonts_icons.cpp
#include "render/fonts_icons.h"
#include "render/palette.h"

namespace rcpaper {

// Use M5GFX's built-in scalable fonts (no asset baking required for v1).
// FreeSans is bundled; bold variants where available.
const lgfx::IFont* font_for(TextSize size, Weight weight) {
  switch (size) {
    case TextSize::H1:
      return weight == Weight::Bold ? &fonts::FreeSansBold24pt7b
                                    : &fonts::FreeSans24pt7b;
    case TextSize::H2:
      return weight == Weight::Bold ? &fonts::FreeSansBold18pt7b
                                    : &fonts::FreeSans18pt7b;
    case TextSize::Body:
      return weight == Weight::Bold ? &fonts::FreeSansBold12pt7b
                                    : &fonts::FreeSans12pt7b;
    case TextSize::Sm:
    default:
      return weight == Weight::Bold ? &fonts::FreeSansBold9pt7b
                                    : &fonts::FreeSans9pt7b;
  }
}

int16_t font_height(TextSize size, Weight weight) {
  switch (size) {
    case TextSize::H1:   return 36;
    case TextSize::H2:   return 28;
    case TextSize::Body: return 18;
    case TextSize::Sm:
    default:             return 14;
  }
}

IconResult draw_icon_placeholder(M5GFX& gfx, int16_t x, int16_t y,
                                 const IconNode& n) {
  int16_t s = n.size > 0 ? n.size : 16;
  gfx.fillRect(x, y, s, s, color_to_m5gfx(n.color));
  return { s, s };
}

}  // namespace rcpaper
```

- [ ] **Step 3: Verify the firmware still builds**

Run: `pio run -e m5stack-papercolor`
Expected: build succeeds (no link errors). No device upload needed yet — primitives in the next task will exercise these.

- [ ] **Step 4: Commit**

```bash
git add src/render/fonts_icons.h src/render/fonts_icons.cpp
git commit -m "feat(fw): font + placeholder-icon registry"
```

---

## Task 6: Paint pass — text/divider/spacer + M5GFX measurer (device)

**Files:**
- Modify: `src/render/engine.h` (add `paint_layout` + concrete `M5GFXMeasurer`)
- Modify: `src/render/engine.cpp` (add paint pass scaffolding)
- Create: `src/render/primitives.cpp` (one function per primitive; text + divider + spacer in this task)

Renders the laid-out tree onto the M5GFX display. Other primitives are stubbed (no-op) and filled in over the next two tasks.

- [ ] **Step 1: Extend `engine.h`**

Append to the bottom of `src/render/engine.h`:

```cpp
namespace rcpaper {

#ifdef ARDUINO
class M5GFXMeasurer : public Measurer {
 public:
  explicit M5GFXMeasurer(M5GFX& gfx) : gfx_(gfx) {}
  Size text_size(const TextNode& t) const override;
  Size icon_size(const IconNode& i) const override;
  Size image_size(const ImageNode& i) const override;
 private:
  M5GFX& gfx_;
};

void paint_layout(M5GFX& gfx, const LaidOutNode& laid);
#endif

}  // namespace rcpaper
```

- [ ] **Step 2: Implement `M5GFXMeasurer` in `engine.cpp`**

Append to `src/render/engine.cpp`, guarded by `#ifdef ARDUINO`:

```cpp
#ifdef ARDUINO
#include "render/fonts_icons.h"

namespace rcpaper {

Size M5GFXMeasurer::text_size(const TextNode& t) const {
  gfx_.setFont(font_for(t.size, t.weight));
  int16_t w = gfx_.textWidth(t.value.c_str()) + 2 * t.pad;
  int16_t h = font_height(t.size, t.weight) + 2 * t.pad;
  return Size{w, h};
}

Size M5GFXMeasurer::icon_size(const IconNode& i) const {
  return Size{i.size, i.size};
}

Size M5GFXMeasurer::image_size(const ImageNode& i) const {
  return Size{i.width, i.height};
}

// Forward declarations from primitives.cpp
void draw_text(M5GFX&, const TextNode&, Rect);
void draw_divider(M5GFX&, const DividerNode&, Rect, bool vertical_main);
void draw_hbar(M5GFX&, const HBarNode&, Rect);
void draw_icon(M5GFX&, const IconNode&, Rect);
void draw_image(M5GFX&, const ImageNode&, Rect);
void draw_sparkline(M5GFX&, const SparklineNode&, Rect);
void draw_marker(M5GFX&, const MarkerNode&, Rect);

static void paint_node(M5GFX& gfx, const LaidOutNode& laid) {
  if (!laid.node) return;
  switch (laid.node->type) {
    case NodeType::VStack:
    case NodeType::HStack:
      for (const auto& c : laid.children) paint_node(gfx, c);
      break;
    case NodeType::Spacer:
      // no-op
      break;
    case NodeType::Divider:
      draw_divider(gfx, laid.node->as_divider, laid.rect, /*vertical_main=*/false);
      break;
    case NodeType::Text:      draw_text(gfx,      laid.node->as_text,      laid.rect); break;
    case NodeType::HBar:      draw_hbar(gfx,      laid.node->as_hbar,      laid.rect); break;
    case NodeType::Icon:      draw_icon(gfx,      laid.node->as_icon,      laid.rect); break;
    case NodeType::Image:     draw_image(gfx,     laid.node->as_image,     laid.rect); break;
    case NodeType::Sparkline: draw_sparkline(gfx, laid.node->as_sparkline, laid.rect); break;
    case NodeType::Marker:    draw_marker(gfx,    laid.node->as_marker,    laid.rect); break;
    case NodeType::Unknown:
    default: break;
  }
}

void paint_layout(M5GFX& gfx, const LaidOutNode& root) {
  paint_node(gfx, root);
}

}  // namespace rcpaper
#endif
```

- [ ] **Step 3: Implement `primitives.cpp` (text + divider stubs for others)**

```cpp
// src/render/primitives.cpp
#include <M5GFX.h>
#include "render/engine.h"
#include "render/fonts_icons.h"
#include "render/palette.h"
#include "proto/schema.h"

namespace rcpaper {

void draw_text(M5GFX& gfx, const TextNode& t, Rect r) {
  if (t.has_bg) {
    gfx.fillRect(r.x, r.y, r.w, r.h, color_to_m5gfx(t.bg));
  }
  gfx.setFont(font_for(t.size, t.weight));
  gfx.setTextColor(color_to_m5gfx(t.color));

  int16_t tx = r.x + t.pad;
  int16_t ty = r.y + t.pad;
  int16_t inner_w = r.w - 2 * t.pad;
  int16_t text_w = gfx.textWidth(t.value.c_str());

  if (t.align == Align::Center) tx = r.x + (r.w - text_w) / 2;
  else if (t.align == Align::End) tx = r.x + r.w - text_w - t.pad;

  gfx.setCursor(tx, ty);
  gfx.print(t.value.c_str());
}

void draw_divider(M5GFX& gfx, const DividerNode& d, Rect r, bool /*vertical_main*/) {
  gfx.fillRect(r.x, r.y, r.w, d.thickness, color_to_m5gfx(d.color));
}

// Stubs to be implemented in Tasks 7-8
void draw_hbar(M5GFX&, const HBarNode&, Rect) {}
void draw_icon(M5GFX&, const IconNode&, Rect) {}
void draw_image(M5GFX&, const ImageNode&, Rect) {}
void draw_sparkline(M5GFX&, const SparklineNode&, Rect) {}
void draw_marker(M5GFX&, const MarkerNode&, Rect) {}

}  // namespace rcpaper
```

- [ ] **Step 4: Wire a smoke test into `main.cpp` (temporary)**

Replace `setup()` body in `src/main.cpp`:

```cpp
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
```

- [ ] **Step 5: Build, flash, verify on device**

Run:
```bash
pio run -e m5stack-papercolor -t upload
pio device monitor
```

Expected on the EPD: a red header strip "rcpapercolor" in white, a thick black divider line, body text "text + divider live" below. No layout overlap; padding looks symmetric.

- [ ] **Step 6: Commit**

```bash
git add src/render/engine.h src/render/engine.cpp src/render/primitives.cpp src/main.cpp
git commit -m "feat(fw): paint pass with text + divider; M5GFX measurer; smoke render"
```

---

## Task 7: Paint pass — hbar + marker + sparkline (device)

**Files:**
- Modify: `src/render/primitives.cpp` (implement `draw_hbar`, `draw_marker`, `draw_sparkline`)
- Modify: `src/main.cpp` (extend smoke page to include these primitives)

- [ ] **Step 1: Implement `draw_hbar`, `draw_marker`, `draw_sparkline`**

Replace the stubs in `src/render/primitives.cpp`:

```cpp
void draw_hbar(M5GFX& gfx, const HBarNode& b, Rect r) {
  // Border
  uint32_t fg = color_to_m5gfx(b.color);
  gfx.drawRect(r.x, r.y, r.w, r.h, 0x000000);
  // Fill
  int16_t inner_w = r.w - 4;
  int16_t pct = (int16_t)b.pct;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int16_t fill = (int16_t)(((int32_t)inner_w * pct) / 100);
  if (fill > 0) gfx.fillRect(r.x + 2, r.y + 2, fill, r.h - 4, fg);

  // Optional labels — left at the inset, right at the trailing edge.
  if (!b.label_left.empty()) {
    gfx.setFont(font_for(TextSize::Sm, Weight::Bold));
    gfx.setTextColor(0xFFFFFF);
    gfx.setCursor(r.x + 4, r.y + (r.h - 14) / 2);
    gfx.print(b.label_left.c_str());
  }
  if (!b.label_right.empty()) {
    gfx.setFont(font_for(TextSize::Sm, Weight::Bold));
    gfx.setTextColor(0x000000);
    int16_t tw = gfx.textWidth(b.label_right.c_str());
    gfx.setCursor(r.x + r.w - tw - 4, r.y + (r.h - 14) / 2);
    gfx.print(b.label_right.c_str());
  }
}

void draw_marker(M5GFX& gfx, const MarkerNode& m, Rect r) {
  // r covers the previous sibling's rect (passed by engine via overlay layout).
  // We just draw a vertical line at pct of r.w, full height of r.
  int16_t pct = (int16_t)m.pct;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int16_t x = r.x + (int16_t)(((int32_t)r.w * pct) / 100);
  gfx.drawFastVLine(x, r.y - 4, r.h + 8, color_to_m5gfx(m.color));
}

void draw_sparkline(M5GFX& gfx, const SparklineNode& s, Rect r) {
  if (s.values.empty()) return;
  uint32_t fg = color_to_m5gfx(s.color);

  // Baseline + range
  float lo = s.values[0], hi = s.values[0];
  for (float v : s.values) { if (v < lo) lo = v; if (v > hi) hi = v; }
  if (hi == lo) { lo -= 1; hi += 1; }

  auto y_of = [&](float v) -> int16_t {
    float pct = (v - lo) / (hi - lo);
    return (int16_t)(r.y + r.h - (int16_t)(pct * (r.h - 2)) - 1);
  };

  size_t n = s.values.size();
  if (n < 2) {
    gfx.fillCircle(r.x + r.w / 2, y_of(s.values[0]), 2, fg);
    return;
  }
  for (size_t i = 1; i < n; ++i) {
    int16_t x0 = r.x + (int16_t)(((int32_t)(i - 1) * (r.w - 1)) / (int32_t)(n - 1));
    int16_t x1 = r.x + (int16_t)(((int32_t)i       * (r.w - 1)) / (int32_t)(n - 1));
    gfx.drawLine(x0, y_of(s.values[i - 1]), x1, y_of(s.values[i]), fg);
  }
  // Baseline rule
  gfx.drawFastHLine(r.x, r.y + r.h - 1, r.w, 0x000000);
}
```

Note: `marker` is intentionally simple — it draws relative to its OWN rect. The engine currently lays it out with width=1, so by default it sits at the parent's start. For threshold-on-bar use, the producer can place `marker` immediately after an `hbar` inside an `hstack` of width=bar.width; v1 accepts this limitation. A richer "anchor to previous sibling" model is deferred.

- [ ] **Step 2: Extend the smoke page in `main.cpp`**

```cpp
static const char* PAGE = R"({
  "type":"vstack","pad":14,"gap":6,
  "children":[
    {"type":"text","value":"rcpapercolor","size":"h1","color":"white","bg":"red","pad":8,"align":"center"},
    {"type":"divider","color":"black","thickness":2},
    {"type":"text","value":"5H WINDOW","size":"sm","weight":"bold"},
    {"type":"hbar","pct":42,"color":"blue","height":24,"label_right":"42%"},
    {"type":"text","value":"7D WINDOW","size":"sm","weight":"bold"},
    {"type":"hbar","pct":68,"color":"green","height":24,"label_right":"68%"},
    {"type":"text","value":"daily usage","size":"sm","weight":"bold"},
    {"type":"sparkline","values":[12,18,14,22,19,28,31],"color":"blue","height":50}
  ]
})";
```

- [ ] **Step 3: Build, flash, verify**

Run:
```bash
pio run -e m5stack-papercolor -t upload
```

Expected on EPD: header strip + divider as before; two horizontal bars (blue 42%, green 68%) with right-aligned percentages; a sparkline polyline at the bottom with a thin baseline rule. Bars should have a 1-px black border + 2-px inset fill.

- [ ] **Step 4: Commit**

```bash
git add src/render/primitives.cpp src/main.cpp
git commit -m "feat(fw): paint hbar + marker + sparkline; smoke page updated"
```

---

## Task 8: Paint pass — icon + image (device)

**Files:**
- Modify: `src/render/primitives.cpp` (implement `draw_icon`, `draw_image`)
- Modify: `src/main.cpp` (add an icon to the smoke page)

`icon` uses `fonts_icons.h`'s placeholder for v1 (a coloured square). `image` is implemented as a no-op with a warning printf, because PNG decoding on EPD with PSRAM is sizeable scope; the spec lists this as "rare in V1". When a real `image` need surfaces, replace this stub.

- [ ] **Step 1: Implement `draw_icon` and `draw_image`**

Replace the stubs:

```cpp
void draw_icon(M5GFX& gfx, const IconNode& i, Rect r) {
  // Placeholder: filled square of the requested size + colour, centred in rect.
  int16_t s = i.size > 0 ? i.size : (int16_t)std::min(r.w, r.h);
  int16_t x = r.x + (r.w - s) / 2;
  int16_t y = r.y + (r.h - s) / 2;
  gfx.fillRect(x, y, s, s, color_to_m5gfx(i.color));
}

void draw_image(M5GFX& gfx, const ImageNode& i, Rect r) {
  // v1 stub: outline the area + label.
  gfx.drawRect(r.x, r.y, r.w, r.h, 0x000000);
  gfx.setFont(font_for(TextSize::Sm, Weight::Regular));
  gfx.setTextColor(0x000000);
  gfx.setCursor(r.x + 4, r.y + 4);
  gfx.printf("img:%s", i.name.c_str());
}
```

Add `#include <algorithm>` at the top of `primitives.cpp` if not already present.

- [ ] **Step 2: Extend smoke page with an icon**

Add to the children array in `src/main.cpp`'s `PAGE` constant, near the header:

```cpp
{"type":"hstack","align":"center","gap":8,"children":[
  {"type":"icon","name":"claude","size":24,"color":"red"},
  {"type":"text","value":"rcpapercolor","size":"h1","color":"white","bg":"red","pad":8}
]},
```

(Remove the original standalone header text node to avoid duplication.)

- [ ] **Step 3: Build, flash, verify**

Run: `pio run -e m5stack-papercolor -t upload`
Expected: a red square next to "rcpapercolor" in the header row; other primitives unchanged.

- [ ] **Step 4: Commit**

```bash
git add src/render/primitives.cpp src/main.cpp
git commit -m "feat(fw): paint icon (placeholder) + image (stub)"
```

---

## Task 9: WiFi helper — `net/wifi.{h,cpp}`

**Files:**
- Create: `src/net/wifi.h`
- Create: `src/net/wifi.cpp`

Single function: connect with up to N attempts, return bool. Reuses settings from `config.h`.

- [ ] **Step 1: Implement `wifi.h`**

```cpp
// src/net/wifi.h
#pragma once
#include <stdbool.h>

namespace rcpaper {
bool wifi_connect(int max_attempts = 30);
void wifi_disconnect();
}  // namespace rcpaper
```

- [ ] **Step 2: Implement `wifi.cpp`**

```cpp
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
```

- [ ] **Step 3: Smoke-test in `main.cpp`**

Temporarily add to `setup()` after `M5.begin(cfg)`:

```cpp
#include "net/wifi.h"
// ...
rcpaper::wifi_connect();
```

- [ ] **Step 4: Build, flash, verify**

Run: `pio run -e m5stack-papercolor -t upload`
Expected: serial log shows `[wifi] connected, IP=...` within ~5 s.

- [ ] **Step 5: Commit**

```bash
git add src/net/wifi.h src/net/wifi.cpp src/main.cpp
git commit -m "feat(fw): WiFi connect helper"
```

---

## Task 10: Axiom client — `net/axiom.{h,cpp}`

**Files:**
- Create: `src/net/axiom.h`
- Create: `src/net/axiom.cpp`

Posts `{"apl":..., "startTime":..., "endTime":...}` to `AXIOM_QUERY_URL`. Parses `matches[*].data` into a `vector<PageRecord>` where each record holds raw JSON for `layout` plus `id` and `order`. The layout JSON is kept as a `String` so the caller can re-parse it via `parse_layout()` when rendering.

- [ ] **Step 1: Implement `axiom.h`**

```cpp
// src/net/axiom.h
#pragma once
#include <Arduino.h>
#include <vector>

namespace rcpaper {

struct PageRecord {
  String id;
  int    order = 0;
  String layout_json;   // raw JSON substring; parsed on-demand by the renderer.
};

// Returns true on success and fills `out`. On failure, `out` is unchanged.
bool axiom_fetch_pages(std::vector<PageRecord>& out);

}  // namespace rcpaper
```

- [ ] **Step 2: Implement `axiom.cpp`**

```cpp
// src/net/axiom.cpp
#include "net/axiom.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include "config.h"

namespace rcpaper {

static void iso_minus(time_t now, time_t delta, char* out, size_t n) {
  time_t t = now - delta;
  struct tm tm_;
  gmtime_r(&t, &tm_);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tm_);
}

bool axiom_fetch_pages(std::vector<PageRecord>& out) {
  WiFiClientSecure client;
  client.setCACert(AXIOM_ROOT_CA);

  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(client, AXIOM_QUERY_URL)) {
    Serial.println("[axiom] http.begin failed");
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + AXIOM_TOKEN);
  http.addHeader("Content-Type", "application/json");

  time_t now;
  time(&now);
  char start[32], end[32];
  iso_minus(now, 60 * 60, start, sizeof(start));   // 1h lookback (APL also filters)
  iso_minus(now, 0,       end,   sizeof(end));

  JsonDocument req;
  req["apl"]       = AXIOM_APL_QUERY;
  req["startTime"] = start;
  req["endTime"]   = end;
  String body;
  serializeJson(req, body);

  int code = http.POST(body);
  Serial.printf("[axiom] POST -> %d\n", code);
  if (code != 200) {
    if (code > 0) Serial.println(http.getString());
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  // Parse the OUTER response only; keep `layout` as raw JSON.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[axiom] outer parse: %s\n", err.c_str());
    return false;
  }

  out.clear();
  JsonArrayConst matches = doc["matches"].as<JsonArrayConst>();
  if (matches.isNull()) {
    Serial.println("[axiom] no matches[] in response");
    return false;
  }
  for (JsonVariantConst m : matches) {
    JsonVariantConst data = m["data"];
    if (data.isNull()) continue;
    PageRecord rec;
    rec.id    = (const char*)(data["id"] | "");
    rec.order = data["order"] | 0;
    String layout_str;
    serializeJson(data["layout"], layout_str);
    rec.layout_json = layout_str;
    out.push_back(std::move(rec));
  }
  Serial.printf("[axiom] %u pages\n", (unsigned)out.size());
  return true;
}

}  // namespace rcpaper
```

- [ ] **Step 3: Smoke-test in `main.cpp`**

Temporarily replace the smoke page with an Axiom-driven render:

```cpp
#include "net/axiom.h"
// ... after wifi_connect():
configTime(0, 0, "pool.ntp.org");
struct tm ti;
for (int i = 0; i < 20 && !getLocalTime(&ti); ++i) delay(500);

std::vector<rcpaper::PageRecord> pages;
if (rcpaper::axiom_fetch_pages(pages) && !pages.empty()) {
  // Render the first page
  JsonDocument doc;
  deserializeJson(doc, pages[0].layout_json);
  auto root = rcpaper::parse_layout(doc.as<JsonVariantConst>());
  rcpaper::M5GFXMeasurer m(M5.Display);
  M5.Display.fillScreen(0xFFFFFF);
  auto laid = rcpaper::run_layout(root,
                                   rcpaper::Rect{0,0,
                                                 (int16_t)M5.Display.width(),
                                                 (int16_t)M5.Display.height()},
                                   m);
  rcpaper::paint_layout(M5.Display, laid);
  M5.Display.display();
} else {
  Serial.println("[main] no pages from axiom");
}
```

- [ ] **Step 4: Build, flash, verify (requires the producer Task 8 to be running)**

Run:
```bash
# (on Mac) make sure the producer has populated Axiom in the last hour
cd ../producer && uv run claude-fetch && uv run codex-fetch

# (back to firmware)
pio run -e m5stack-papercolor -t upload
pio device monitor
```

Expected: serial shows `[axiom] POST -> 200`, `[axiom] N pages` where N ≥ 1; the EPD renders the Claude (or Codex) page that the producer wrote.

- [ ] **Step 5: Commit**

```bash
git add src/net/axiom.h src/net/axiom.cpp src/main.cpp
git commit -m "feat(fw): Axiom APL client, fetch pages end-to-end"
```

---

## Task 11: NVS persistence — `store/nvs.{h,cpp}`

**Files:**
- Create: `src/store/nvs.h`
- Create: `src/store/nvs.cpp`

Persists across deep sleep: cached pages blob (raw JSON of the matches array), current page index, last fetch epoch. Uses `Preferences` (Arduino's NVS wrapper).

- [ ] **Step 1: Implement `nvs.h`**

```cpp
// src/store/nvs.h
#pragma once
#include <Arduino.h>
#include <vector>
#include "net/axiom.h"   // for PageRecord

namespace rcpaper {

struct DeviceState {
  std::vector<PageRecord> pages;
  uint8_t  page_index   = 0;
  uint64_t last_fetch_s = 0;   // epoch seconds; 0 = never
};

void nvs_init();
DeviceState nvs_load();
void nvs_save(const DeviceState& s);
void nvs_save_index(uint8_t idx);

}  // namespace rcpaper
```

- [ ] **Step 2: Implement `nvs.cpp`**

```cpp
// src/store/nvs.cpp
#include "store/nvs.h"
#include <Preferences.h>
#include <ArduinoJson.h>

namespace rcpaper {

static const char* NS = "rcpaper";

void nvs_init() { /* nothing; Preferences auto-init on begin() */ }

DeviceState nvs_load() {
  DeviceState s;
  Preferences p;
  p.begin(NS, true);
  s.page_index   = p.getUChar("idx", 0);
  s.last_fetch_s = p.getULong64("last", 0);
  String blob = p.getString("pages", "");
  p.end();

  if (blob.length() == 0) return s;

  JsonDocument doc;
  if (deserializeJson(doc, blob)) return s;
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  if (arr.isNull()) return s;
  for (JsonVariantConst v : arr) {
    PageRecord rec;
    rec.id    = (const char*)(v["id"] | "");
    rec.order = v["order"] | 0;
    String layout_str;
    serializeJson(v["layout"], layout_str);
    rec.layout_json = layout_str;
    s.pages.push_back(std::move(rec));
  }
  return s;
}

void nvs_save(const DeviceState& s) {
  // Re-serialize pages to a compact JSON blob.
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& rec : s.pages) {
    JsonObject o = arr.add<JsonObject>();
    o["id"]    = rec.id.c_str();
    o["order"] = rec.order;
    JsonDocument inner;
    deserializeJson(inner, rec.layout_json);
    o["layout"] = inner.as<JsonVariantConst>();
  }
  String blob;
  serializeJson(doc, blob);

  Preferences p;
  p.begin(NS, false);
  p.putString("pages", blob);
  p.putUChar("idx", s.page_index);
  p.putULong64("last", s.last_fetch_s);
  p.end();
  Serial.printf("[nvs] saved %u pages, blob=%u B, idx=%u\n",
                (unsigned)s.pages.size(), blob.length(), s.page_index);
}

void nvs_save_index(uint8_t idx) {
  Preferences p;
  p.begin(NS, false);
  p.putUChar("idx", idx);
  p.end();
}

}  // namespace rcpaper
```

- [ ] **Step 3: Smoke-test by save-then-load in `main.cpp`**

Add temporarily after the axiom fetch in `setup()`:

```cpp
#include "store/nvs.h"
// ...
rcpaper::DeviceState st;
st.pages = pages;
st.page_index = 0;
st.last_fetch_s = (uint64_t)time(nullptr);
rcpaper::nvs_save(st);

auto re = rcpaper::nvs_load();
Serial.printf("[main] reloaded %u pages, idx=%u\n",
              (unsigned)re.pages.size(), re.page_index);
```

- [ ] **Step 4: Build, flash, verify**

Expected: serial logs show `[nvs] saved N pages, blob=... B, idx=0` and `[main] reloaded N pages, idx=0`. Reboot the device (unplug/replug or `pio device monitor` reset) and re-run — pages should reload without re-querying Axiom (until we wire fetch-on-stale in the main loop).

- [ ] **Step 5: Commit**

```bash
git add src/store/nvs.h src/store/nvs.cpp src/main.cpp
git commit -m "feat(fw): NVS-backed cached pages + state"
```

---

## Task 12: Sleep + wake — `power/sleep.{h,cpp}`

**Files:**
- Create: `src/power/sleep.h`
- Create: `src/power/sleep.cpp`

PaperColor buttons are on G1 / G9 / G10 (all RTC GPIOs on ESP32-S3). We wake on a timer (next of `last_fetch + FETCH_INTERVAL`, `now + ROTATE_INTERVAL`) and on any button.

- [ ] **Step 1: Implement `sleep.h`**

```cpp
// src/power/sleep.h
#pragma once
#include <Arduino.h>
#include <esp_sleep.h>

namespace rcpaper {

enum class WakeReason : uint8_t {
  PowerOn = 0,
  Timer,
  ButtonA, ButtonB, ButtonC,
  Unknown,
};

WakeReason wake_reason_from_esp(esp_sleep_wakeup_cause_t cause, uint64_t ext1_mask);
WakeReason current_wake_reason();
void enter_deep_sleep_until(uint64_t epoch_seconds);

}  // namespace rcpaper
```

- [ ] **Step 2: Implement `sleep.cpp`**

```cpp
// src/power/sleep.cpp
#include "power/sleep.h"
#include <time.h>

// Pin mapping from the PaperColor obsidian note.
#define BTN_A_GPIO 9
#define BTN_B_GPIO 10
#define BTN_C_GPIO 1
#define BTN_WAKE_MASK ((1ULL << BTN_A_GPIO) | (1ULL << BTN_B_GPIO) | (1ULL << BTN_C_GPIO))

namespace rcpaper {

WakeReason wake_reason_from_esp(esp_sleep_wakeup_cause_t cause, uint64_t ext1_mask) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER: return WakeReason::Timer;
    case ESP_SLEEP_WAKEUP_EXT1: {
      if (ext1_mask & (1ULL << BTN_A_GPIO)) return WakeReason::ButtonA;
      if (ext1_mask & (1ULL << BTN_B_GPIO)) return WakeReason::ButtonB;
      if (ext1_mask & (1ULL << BTN_C_GPIO)) return WakeReason::ButtonC;
      return WakeReason::Unknown;
    }
    case ESP_SLEEP_WAKEUP_UNDEFINED: return WakeReason::PowerOn;
    default: return WakeReason::Unknown;
  }
}

WakeReason current_wake_reason() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  uint64_t mask = esp_sleep_get_ext1_wakeup_status();
  return wake_reason_from_esp(cause, mask);
}

void enter_deep_sleep_until(uint64_t epoch_seconds) {
  time_t now;
  time(&now);
  int64_t delta = (int64_t)epoch_seconds - (int64_t)now;
  if (delta < 10) delta = 10;  // floor at 10 s

  esp_sleep_enable_timer_wakeup((uint64_t)delta * 1000000ULL);
  esp_sleep_enable_ext1_wakeup(BTN_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);

  Serial.printf("[sleep] entering deep sleep for %lld s\n", (long long)delta);
  Serial.flush();
  esp_deep_sleep_start();
}

}  // namespace rcpaper
```

- [ ] **Step 3: Smoke-test (manual)**

Append to `setup()`:

```cpp
#include "power/sleep.h"
// ... at the end:
rcpaper::WakeReason wr = rcpaper::current_wake_reason();
Serial.printf("[main] wake reason = %u\n", (unsigned)wr);
// Sleep for 30 s; verify timer wake AND button wake both work.
rcpaper::enter_deep_sleep_until((uint64_t)time(nullptr) + 30);
```

Run: `pio run -e m5stack-papercolor -t upload && pio device monitor`
Expected: device boots, logs "wake reason = 0" (PowerOn), prints "[sleep] entering deep sleep for 30 s", then 30 s later wakes again (reason = 1, Timer). Press any of BtnA/B/C during the 30 s — device wakes earlier with reason 2/3/4.

- [ ] **Step 4: Commit**

```bash
git add src/power/sleep.h src/power/sleep.cpp src/main.cpp
git commit -m "feat(fw): deep sleep with RTC timer + button wake"
```

---

## Task 13: Main loop wire-up

**Files:**
- Modify: `src/main.cpp`

Replace the smoke-test contents with the production loop sketched in the spec: on wake, decide whether to fetch, possibly advance/retreat page_index based on wake reason, render the current page (with stale badge), save state, sleep until the next of fetch-time or rotate-time.

- [ ] **Step 1: Rewrite `src/main.cpp`**

```cpp
// src/main.cpp
#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <time.h>

#include "config.h"
#include "net/wifi.h"
#include "net/axiom.h"
#include "proto/parser.h"
#include "render/engine.h"
#include "store/nvs.h"
#include "power/sleep.h"

using namespace rcpaper;

static void sync_ntp_once() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm ti;
  for (int i = 0; i < 20 && !getLocalTime(&ti); ++i) delay(500);
}

static void render_page(const PageRecord& rec, bool stale) {
  JsonDocument doc;
  if (deserializeJson(doc, rec.layout_json)) {
    Serial.println("[render] inner parse failed");
    return;
  }
  LayoutNode root = parse_layout(doc.as<JsonVariantConst>());

  M5GFXMeasurer m(M5.Display);
  M5.Display.fillScreen(0xFFFFFF);
  Rect vp{0, 0, (int16_t)M5.Display.width(), (int16_t)M5.Display.height()};

  auto laid = run_layout(root, vp, m);
  paint_layout(M5.Display, laid);

  if (stale) {
    M5.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5.Display.setTextColor(0xD92027);
    M5.Display.setCursor(8, vp.h - 18);
    M5.Display.print("STALE");
  }
  M5.Display.display();
}

static void render_error(const char* msg) {
  M5.Display.fillScreen(0xFFFFFF);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(0x000000);
  M5.Display.setCursor(20, 40);
  M5.Display.print("rcpapercolor");
  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.setCursor(20, 80);
  M5.Display.print(msg);
  M5.Display.display();
}

void setup() {
  Serial.begin(115200);
  delay(50);

  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power  = true;
  M5.begin(cfg);
  M5.Display.setEpdMode(epd_mode_t::epd_text);

  WakeReason wr = current_wake_reason();
  Serial.printf("[main] wake reason = %u\n", (unsigned)wr);

  DeviceState state = nvs_load();
  time_t now;
  time(&now);

  bool needs_fetch =
      state.pages.empty()
      || wr == WakeReason::ButtonC
      || (uint64_t)now - state.last_fetch_s >= FETCH_INTERVAL_SEC;

  if (needs_fetch) {
    if (!wifi_connect()) {
      render_error("WiFi failed; keeping cached pages");
    } else {
      sync_ntp_once();
      std::vector<PageRecord> fresh;
      if (axiom_fetch_pages(fresh) && !fresh.empty()) {
        state.pages        = std::move(fresh);
        state.last_fetch_s = (uint64_t)time(nullptr);
        state.page_index   = 0;  // land on first page after fetch
        nvs_save(state);
      } else {
        Serial.println("[main] fetch failed; using cache");
      }
      wifi_disconnect();
    }
  }

  // Rotate based on wake reason
  if (!state.pages.empty()) {
    if (wr == WakeReason::ButtonA || wr == WakeReason::Timer) {
      state.page_index = (uint8_t)((state.page_index + 1) % state.pages.size());
    } else if (wr == WakeReason::ButtonB) {
      state.page_index = (uint8_t)((state.page_index + state.pages.size() - 1)
                                    % state.pages.size());
    }
    nvs_save_index(state.page_index);
  }

  // Render
  if (state.pages.empty()) {
    render_error("no pages — check producer + Axiom dataset");
  } else {
    bool stale = (uint64_t)time(nullptr) - state.last_fetch_s > FETCH_INTERVAL_SEC;
    render_page(state.pages[state.page_index], stale);
  }

  // Compute next wake
  uint64_t next_fetch  = state.last_fetch_s + FETCH_INTERVAL_SEC;
  uint64_t next_rotate = (uint64_t)time(nullptr) + ROTATE_INTERVAL_SEC;
  uint64_t next_wake   = next_fetch < next_rotate ? next_fetch : next_rotate;

  enter_deep_sleep_until(next_wake);
}

void loop() { /* unreachable: setup() ends in deep sleep */ }
```

- [ ] **Step 2: Build, flash, verify the full flow**

Run:
```bash
# Ensure producer wrote something in the last hour:
cd ../producer && uv run claude-fetch && uv run codex-fetch && cd ../

pio run -e m5stack-papercolor -t upload
pio device monitor
```

Expected on first boot: serial logs `[main] wake reason = 0` (PowerOn) → WiFi → NTP → `[axiom] POST -> 200` → `[axiom] N pages` → `[nvs] saved …` → first page renders on EPD → `[sleep] entering deep sleep for ~120 s`. After ~2 min device wakes (Timer), advances index, renders the next page, sleeps. Press BtnC at any time → device wakes, re-fetches, re-renders page 0.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat(fw): production wake/fetch/render/sleep loop"
```

---

## Task 14: Stale badge polish + README

**Files:**
- Modify: `src/main.cpp` (improve the stale badge layout)
- Modify: `README.md` (root README — replace empty/placeholder)
- Create: `README.md` if not present

- [ ] **Step 1: Polish the stale badge**

Replace the `if (stale) { ... }` block in `render_page()`:

```cpp
if (stale) {
  // "STALE" pill in the bottom-right
  M5.Display.setFont(&fonts::FreeSansBold9pt7b);
  const char* label = "STALE";
  int16_t tw = M5.Display.textWidth(label);
  int16_t pw = tw + 12, ph = 18;
  int16_t px = vp.w - pw - 8;
  int16_t py = vp.h - ph - 6;
  M5.Display.fillRect(px, py, pw, ph, 0xD92027);   // red bg
  M5.Display.setTextColor(0xFFFFFF);
  M5.Display.setCursor(px + 6, py + 3);
  M5.Display.print(label);
}
```

- [ ] **Step 2: Write/replace top-level `README.md`**

```markdown
# rcpapercolor

Battery-powered colour e-ink dashboard for the M5Stack PaperColor that renders
a generic JSON layout-tree fetched from Axiom. Producers run on macOS and push
one event per dashboard page; the device wakes every 15 min, fetches, renders,
and deep-sleeps.

Two halves of one repo:

- `producer/` — Python (uv) on Mac. Pulls Claude/Codex usage, pushes pages.
  See `producer/README.md` for setup.
- `src/` + `platformio.ini` — ESP32-S3 firmware. PlatformIO + M5Unified +
  M5GFX. See below.

## Firmware quickstart

1. Copy `src/config.h.example` to `src/config.h`. Fill in WiFi credentials,
   Axiom token, and the Axiom Root CA PEM.
2. Make sure the producer has written at least one event to the
   `rcpapercolor` Axiom dataset (run `cd producer && uv run claude-fetch`).
3. Build + flash:

   ```bash
   pio run -e m5stack-papercolor -t upload
   pio device monitor
   ```

4. Device boot sequence (first time): WiFi → NTP → Axiom query → render →
   deep sleep. Subsequent wakes are timer- or button-driven.

## Buttons

- **BtnA** — next page
- **BtnB** — previous page
- **BtnC** — force re-fetch from Axiom now

## Native tests

```bash
pio test -e native
```

Covers: colour palette, JSON parser, layout engine. Rendering, WiFi, NVS, and
sleep are device-bound and verified on hardware as part of the implementation
plan (`docs/superpowers/plans/2026-05-17-rcpapercolor-firmware.md`).

## Design + plans

- Spec: `docs/superpowers/specs/2026-05-17-rcpapercolor-design.md`
- Producer plan: `docs/superpowers/plans/2026-05-17-rcpapercolor-producer.md`
- Firmware plan: `docs/superpowers/plans/2026-05-17-rcpapercolor-firmware.md`
```

- [ ] **Step 3: Run all native tests one last time**

Run: `pio test -e native`
Expected: all three native test suites green.

- [ ] **Step 4: Build firmware one last time**

Run: `pio run -e m5stack-papercolor`
Expected: builds clean; no warnings about deprecated APIs or unused symbols in our own code.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp README.md
git commit -m "polish(fw): stale badge as red pill; top-level README"
```

---

## Self-Review Checklist

- [ ] **Spec coverage:**
  - PlatformIO + M5Unified/M5GFX/M5PM1 stack ✅ (Task 1)
  - Spectra 6 named palette ✅ (Task 2)
  - All v1 primitives ✅ (Tasks 3 + 6 + 7 + 8)
  - Layout engine (flex) ✅ (Task 4)
  - WiFi → Axiom APL → parse → render ✅ (Tasks 9, 10, 13)
  - NVS-backed cached pages, page_index, last_fetch_ts ✅ (Task 11)
  - RTC timer + button ext1 deep sleep ✅ (Task 12)
  - BtnA=next, BtnB=prev, BtnC=force fetch ✅ (Task 13)
  - Stale badge surfaces in render ✅ (Tasks 13, 14)
  - `config.h.example` vs gitignored `config.h` ✅ (Task 1 + repo `.gitignore`)
  - Error frames ("no pages", "WiFi failed") ✅ (Task 13)

- [ ] **Placeholders:** none — every step has code or commands.

- [ ] **Type consistency:**
  - `Color` enum, `NodeType` enum, `TextSize`/`Weight`/`Align` enums — same
    spellings across `palette.h`, `schema.h`, `parser.cpp`, `primitives.cpp`,
    `engine.cpp`.
  - `Rect{x,y,w,h}` and `Size{w,h}` used consistently in engine + primitives.
  - `PageRecord{id, order, layout_json}` defined in `net/axiom.h`, consumed
    identically by `store/nvs.cpp` and `main.cpp`.
  - `WakeReason` enum values referenced identically in `power/sleep.cpp` and
    `main.cpp` (Timer/ButtonA/ButtonB/ButtonC/PowerOn/Unknown).

- [ ] **Known limitation (called out in spec, acknowledged in plan):**
  - `marker` node placement is simple (lays out at width=1 inside the parent
    stack rather than anchoring to a previous sibling's `hbar`). Acceptable
    for v1; richer anchoring deferred to a follow-up.
  - `image` primitive is a stub (outline + label). Real PNG decoding deferred
    until the first dashboard that actually needs it.
