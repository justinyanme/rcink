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
