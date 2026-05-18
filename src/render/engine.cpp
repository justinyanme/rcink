#include "render/engine.h"
#include <algorithm>

namespace rcpaper {

// Forward declarations
static LaidOutNode layout_node(const LayoutNode& n, Rect viewport, const Measurer& m);
static LaidOutNode layout_stack(const LayoutNode& root, Rect viewport, const Measurer& m);

// Sentinel: a main axis size at or above this is treated as "unbounded"
// — i.e. we're inside a probe pass whose caller doesn't know its size yet.
// In that case flex children must NOT inflate to fill, or their flexed
// width gets accumulated into the parent's `fixed_main` and overflows int16.
// Anything below this is still ample for real layouts (the panel is 540 px).
static constexpr int16_t UNBOUNDED_THRESHOLD = 0x4000;

// Compute natural main/cross size for a leaf node.
// parent_cross is the available cross-axis dimension (used by fill-parent nodes).
static Size natural_size(const LayoutNode& n, const Measurer& m, int16_t parent_cross, bool vertical) {
  switch (n.type) {
    case NodeType::Text:      return m.text_size(n.as_text);
    case NodeType::Icon:      return m.icon_size(n.as_icon);
    case NodeType::Image:     return m.image_size(n.as_image);
    case NodeType::Spacer:
      if (n.as_spacer.has_size) return Size{n.as_spacer.size, n.as_spacer.size};
      return Size{0, 0};  // flex; caller handles separately
    case NodeType::Divider:
      return vertical ? Size{parent_cross, n.as_divider.thickness}
                      : Size{n.as_divider.thickness, parent_cross};
    case NodeType::HBar:
      return vertical ? Size{parent_cross, n.as_hbar.height}
                      : Size{n.as_hbar.height, parent_cross};
    case NodeType::Sparkline:
      return vertical ? Size{parent_cross, n.as_sparkline.height}
                      : Size{n.as_sparkline.height, parent_cross};
    case NodeType::Marker:    return Size{1, 0};
    case NodeType::VStack:
    case NodeType::HStack:    return Size{0, 0};  // resolved recursively
    case NodeType::Unknown:
    default:                  return Size{0, 0};
  }
}

static LaidOutNode layout_leaf(const LayoutNode& n, Rect vp, const Measurer&) {
  LaidOutNode out;
  out.node = &n;
  out.rect = vp;
  return out;
}

static LaidOutNode layout_node(const LayoutNode& n, Rect vp, const Measurer& m) {
  if (n.type == NodeType::VStack || n.type == NodeType::HStack)
    return layout_stack(n, vp, m);
  return layout_leaf(n, vp, m);
}

static LaidOutNode layout_stack(const LayoutNode& root, Rect viewport, const Measurer& m) {
  const bool vertical = (root.type == NodeType::VStack);
  const auto& s = root.as_stack;

  // Apply padding to get inner rect.
  Rect inner{
    (int16_t)(viewport.x + s.pad),
    (int16_t)(viewport.y + s.pad),
    (int16_t)(viewport.w - 2 * s.pad),
    (int16_t)(viewport.h - 2 * s.pad),
  };

  // Available space along main / cross axes.
  int16_t cross_avail = vertical ? inner.w : inner.h;
  int16_t main_avail  = vertical ? inner.h : inner.w;

  const int n_children = static_cast<int>(s.children.size());

  // First pass: measure each child's main and cross size; count flex spacers.
  std::vector<int16_t> child_main(n_children, 0);
  std::vector<int16_t> child_cross(n_children, 0);
  std::vector<bool>    is_flex(n_children, false);
  int   flex_count  = 0;
  int16_t fixed_main = 0;
  int16_t max_cross  = 0;  // used as container row/col size for cross alignment

  // HBar / Sparkline / Divider flex along the perpendicular of their own
  // thickness: in an HStack they grow horizontally to fill remaining width,
  // and their `height` / `thickness` property is the *cross-axis* extent,
  // not their main size. (In a VStack they still report cross_avail as width
  // and `height` / `thickness` as height, so the existing path still works.)
  //
  // Divider has to be in this list too: without it, an `hstack([…, divider,
  // …])` pattern (e.g. editorial mastheads) trips natural_size's parent_cross
  // path during the outer vstack's probe, where parent_cross = 0x7FFF; max_cross
  // then blows up to int16 max and every sibling row gets pushed off-screen.
  auto child_is_flex_main = [&](const LayoutNode& c) {
    if (c.type == NodeType::Spacer && !c.as_spacer.has_size) return true;
    if (!vertical && (c.type == NodeType::HBar
                   || c.type == NodeType::Sparkline
                   || c.type == NodeType::Divider)) return true;
    return false;
  };

  for (int i = 0; i < n_children; ++i) {
    const auto& c = s.children[i];

    if (child_is_flex_main(c)) {
      flex_count++;
      is_flex[i] = true;
      // For HBar/Sparkline/Divider in HStack: cross-axis size = their thickness.
      if (c.type == NodeType::HBar)       child_cross[i] = c.as_hbar.height;
      if (c.type == NodeType::Sparkline)  child_cross[i] = c.as_sparkline.height;
      if (c.type == NodeType::Divider)    child_cross[i] = c.as_divider.thickness;
      if (child_cross[i] > 0) max_cross = std::max(max_cross, child_cross[i]);
      continue;  // main size determined later
    }

    Size sz;
    if (c.type == NodeType::VStack || c.type == NodeType::HStack) {
      // Probe-layout nested stack to determine its natural extent. Pass
      // probing=true so any flex descendants don't inflate to 0x7FFF and
      // overflow int16 when summed; they contribute 0 main-size in probe
      // mode and only flex on the real layout pass with a finite parent.
      Rect probe = vertical
        ? Rect{0, 0, cross_avail, 0x7FFF}
        : Rect{0, 0, 0x7FFF, cross_avail};
      LaidOutNode laid = layout_stack(c, probe, m);
      // Measure used main extent from children of the nested stack.
      int16_t used_main  = 0;
      int16_t used_cross = 0;
      for (const auto& ch : laid.children) {
        int16_t end_main  = vertical
          ? (int16_t)(ch.rect.y - laid.rect.y + ch.rect.h)
          : (int16_t)(ch.rect.x - laid.rect.x + ch.rect.w);
        int16_t end_cross = vertical
          ? (int16_t)(ch.rect.x - laid.rect.x + ch.rect.w)
          : (int16_t)(ch.rect.y - laid.rect.y + ch.rect.h);
        used_main  = std::max(used_main,  end_main);
        used_cross = std::max(used_cross, end_cross);
      }
      sz = vertical ? Size{used_cross, used_main} : Size{used_main, used_cross};
    } else {
      sz = natural_size(c, m, cross_avail, vertical);
    }

    child_main[i]  = vertical ? sz.h : sz.w;
    child_cross[i] = vertical ? sz.w : sz.h;
    fixed_main    += child_main[i];
    max_cross      = std::max(max_cross, child_cross[i]);
  }

  // Gaps between non-zero-height children.
  int16_t gaps_total = n_children > 1
    ? (int16_t)((n_children - 1) * s.gap)
    : (int16_t)0;

  // Distribute remaining space to flex spacers. If main_avail is at the
  // unbounded sentinel (caller is probing this stack to measure its natural
  // extent), flex children must NOT inflate — they'd grab 0x7FFF each and
  // overflow the parent's int16 fixed_main accumulator, hiding the sub-tree.
  // Flex only happens when the parent has handed us a real, finite main size.
  const bool unbounded = main_avail >= UNBOUNDED_THRESHOLD;
  int16_t flex_each = 0;
  if (flex_count > 0 && !unbounded) {
    int16_t remaining = (int16_t)(main_avail - fixed_main - gaps_total);
    flex_each = (int16_t)(std::max<int16_t>(0, remaining) / flex_count);
  }

  // Second pass: place children.
  LaidOutNode out;
  out.node = &root;
  out.rect = viewport;

  int16_t cursor = vertical ? inner.y : inner.x;   // main-axis position

  for (int i = 0; i < n_children; ++i) {
    const auto& c = s.children[i];

    int16_t main_sz, cross_sz;

    if (is_flex[i]) {
      main_sz  = flex_each;
      // For HBar/Sparkline child_cross[i] is the thickness; for an invisible
      // flex Spacer pin to max_cross (the largest fixed sibling). Using
      // cross_avail blows up during the parent's probe pass — cross_avail is
      // 0x7FFF when probing, so the spacer's rect.h would dominate the
      // parent's used_main calculation and overflow int16.
      cross_sz = child_cross[i] > 0 ? child_cross[i] : max_cross;
    } else {
      main_sz  = child_main[i];
      cross_sz = child_cross[i] > 0 ? child_cross[i] : cross_avail;
    }

    // Cross-axis position determined by alignment relative to max_cross.
    int16_t cross_base = vertical ? inner.x : inner.y;
    int16_t cross_pos  = cross_base;
    int16_t ref_cross  = std::max(max_cross, cross_sz);  // container row/col size

    if (s.align == Align::Center) {
      cross_pos = cross_base + (ref_cross - cross_sz) / 2;
    } else if (s.align == Align::End) {
      cross_pos = cross_base + (ref_cross - cross_sz);
    }

    Rect child_vp = vertical
      ? Rect{cross_pos, cursor, cross_sz, main_sz}
      : Rect{cursor, cross_pos, main_sz, cross_sz};

    out.children.push_back(layout_node(c, child_vp, m));

    cursor += main_sz;
    if (i + 1 < n_children) cursor += s.gap;
  }

  return out;
}

LaidOutNode run_layout(const LayoutNode& root, Rect viewport, const Measurer& m) {
  return layout_node(root, viewport, m);
}

}  // namespace rcpaper

#ifdef ARDUINO
#include "render/fonts_icons.h"

namespace rcpaper {

Size M5GFXMeasurer::text_size(const TextNode& t) const {
  // Match the font draw_text() will actually use, otherwise the layout
  // engine reserves the wrong horizontal width for CJK strings.
  const bool needs_cjk = string_needs_cjk(t.value);
  gfx_.setFont(needs_cjk ? cjk_font_for(t.size, t.weight)
                         : font_for(t.size, t.weight));
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
void draw_text(LovyanGFX&, const TextNode&, Rect);
void draw_divider(LovyanGFX&, const DividerNode&, Rect, bool vertical_main);
void draw_hbar(LovyanGFX&, const HBarNode&, Rect);
void draw_icon(LovyanGFX&, const IconNode&, Rect);
void draw_image(LovyanGFX&, const ImageNode&, Rect);
void draw_sparkline(LovyanGFX&, const SparklineNode&, Rect);
void draw_marker(LovyanGFX&, const MarkerNode&, Rect);

static void paint_node(LovyanGFX& gfx, const LaidOutNode& laid) {
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

void paint_layout(LovyanGFX& gfx, const LaidOutNode& root) {
  paint_node(gfx, root);
}

}  // namespace rcpaper
#endif
