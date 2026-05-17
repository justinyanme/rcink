#include "render/engine.h"
#include <algorithm>

namespace rcpaper {

// Forward declarations
static LaidOutNode layout_node(const LayoutNode& n, Rect viewport, const Measurer& m);
static LaidOutNode layout_stack(const LayoutNode& root, Rect viewport, const Measurer& m);

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
  int   flex_count  = 0;
  int16_t fixed_main = 0;
  int16_t max_cross  = 0;  // used as container row/col size for cross alignment

  for (int i = 0; i < n_children; ++i) {
    const auto& c = s.children[i];

    if (c.type == NodeType::Spacer && !c.as_spacer.has_size) {
      flex_count++;
      continue;  // main size determined later
    }

    Size sz;
    if (c.type == NodeType::VStack || c.type == NodeType::HStack) {
      // Probe-layout nested stack to determine its natural extent.
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

  // Distribute remaining space to flex spacers.
  int16_t flex_each = 0;
  if (flex_count > 0) {
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

    if (c.type == NodeType::Spacer && !c.as_spacer.has_size) {
      main_sz  = flex_each;
      cross_sz = cross_avail;
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
