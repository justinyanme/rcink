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

// Regression: divider inside an hstack must flex along the main axis like
// hbar / sparkline. Without that treatment, natural_size returns parent_cross
// (== 0x7FFF during the outer vstack's probe), max_cross blows up to int16
// max, and every sibling row of the outer vstack gets pushed off-screen.
//
// Pattern: vstack([ hstack([divider, text("MASTHEAD"), divider]), text("BODY") ])
// — the editorial direction's centered-text-with-rules masthead.
void test_divider_in_hstack_does_not_blow_up_outer_vstack() {
  LayoutNode masthead;
  masthead.type = NodeType::HStack;
  masthead.as_stack.gap = 6;
  masthead.as_stack.align = Align::Center;

  LayoutNode dl; dl.type = NodeType::Divider; dl.as_divider.thickness = 1;
  LayoutNode title; title.type = NodeType::Text; title.as_text.value = "MAST"; // w=40, h=16
  LayoutNode dr; dr.type = NodeType::Divider; dr.as_divider.thickness = 1;
  masthead.as_stack.children = {dl, title, dr};

  LayoutNode body; body.type = NodeType::Text; body.as_text.value = "B"; // w=10, h=16

  LayoutNode root;
  root.type = NodeType::VStack;
  root.as_stack.gap = 4;
  root.as_stack.children = {masthead, body};

  FakeMeasurer m;
  auto layout = run_layout(root, Rect{0, 0, 200, 600}, m);

  TEST_ASSERT_EQUAL(2, layout.children.size());
  // Masthead row must measure as tall as its tallest non-flex child (the title,
  // h=16) — NOT 0x7FFF.
  TEST_ASSERT_EQUAL(16, layout.children[0].rect.h);
  // Body sits immediately after the masthead + gap, well inside the 600px viewport.
  TEST_ASSERT_EQUAL(16 + 4, layout.children[1].rect.y);
  // Dividers in the hstack flex along x: the row of {div, gap, title, gap, div}
  // shares 200 - 40 - 2*6 = 148 between the two dividers ⇒ 74 px each.
  TEST_ASSERT_EQUAL(3, layout.children[0].children.size());
  const auto& left_div = layout.children[0].children[0];
  const auto& right_div = layout.children[0].children[2];
  TEST_ASSERT_EQUAL(74, left_div.rect.w);
  TEST_ASSERT_EQUAL(1,  left_div.rect.h);  // thickness, not full row height
  TEST_ASSERT_EQUAL(74, right_div.rect.w);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_vstack_basic_layout);
  RUN_TEST(test_flex_spacer_eats_remaining_space);
  RUN_TEST(test_align_center_in_hstack);
  RUN_TEST(test_divider_in_hstack_does_not_blow_up_outer_vstack);
  return UNITY_END();
}
