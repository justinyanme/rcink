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
  s.gap   = v["gap"]  | 0;
  s.pad   = v["pad"]  | 0;
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
      const char* bg      = v["bg"] | (const char*)nullptr;
      if (bg && *bg) { n.as_text.bg = color_from_token(bg); n.as_text.has_bg = true; }
      n.as_text.pad       = v["pad"] | 0;
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
      n.as_image.url    = (const char*)(v["url"] | "");
      n.as_image.format = (const char*)(v["format"] | "");
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
