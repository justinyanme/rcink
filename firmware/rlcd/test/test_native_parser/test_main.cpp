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
	    "align":"center","bg":"red","pad":8,"dy":2
	  })");
  TEST_ASSERT_EQUAL(NodeType::Text, n.type);
  TEST_ASSERT_EQUAL(TextSize::H1, n.as_text.size);
  TEST_ASSERT_EQUAL(Color::White, n.as_text.color);
  TEST_ASSERT_EQUAL(Weight::Bold, n.as_text.weight);
  TEST_ASSERT_EQUAL(Align::Center, n.as_text.align);
	  TEST_ASSERT_EQUAL(Color::Red, n.as_text.bg);
	  TEST_ASSERT_EQUAL(8, n.as_text.pad);
	  TEST_ASSERT_EQUAL(2, n.as_text.dy);
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

void test_remote_image() {
  auto n = parse_str(R"({
    "type":"image",
    "url":"https://example.com/frame.png",
    "format":"png",
    "width":320,
    "height":200
  })");
  TEST_ASSERT_EQUAL(NodeType::Image, n.type);
  TEST_ASSERT_EQUAL_STRING("https://example.com/frame.png", n.as_image.url.c_str());
  TEST_ASSERT_EQUAL_STRING("png", n.as_image.format.c_str());
  TEST_ASSERT_EQUAL(320, n.as_image.width);
  TEST_ASSERT_EQUAL(200, n.as_image.height);
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

void test_invalid_color_becomes_unknown() {
  auto n = parse_str(R"({"type":"text","value":"X","color":"magenta"})");
  TEST_ASSERT_EQUAL(NodeType::Unknown, n.type);

  n = parse_str(R"({"type":"text","value":"X","bg":"gray"})");
  TEST_ASSERT_EQUAL(NodeType::Unknown, n.type);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_text_minimal);
  RUN_TEST(test_text_full);
  RUN_TEST(test_vstack_with_children);
  RUN_TEST(test_spacer_flex);
  RUN_TEST(test_hbar);
  RUN_TEST(test_remote_image);
  RUN_TEST(test_sparkline_values);
  RUN_TEST(test_unknown_type_becomes_unknown);
  RUN_TEST(test_invalid_color_becomes_unknown);
  return UNITY_END();
}
