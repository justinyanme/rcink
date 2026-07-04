#include "render/diagnostics.h"

#include <cstdio>
#include <string>

namespace rcpaper {
namespace {

LayoutNode text_node(const std::string& value,
                     TextSize size = TextSize::Sm,
                     Weight weight = Weight::Regular,
                     Color color = Color::Black) {
  LayoutNode node;
  node.type = NodeType::Text;
  node.as_text.value = value;
  node.as_text.size = size;
  node.as_text.weight = weight;
  node.as_text.color = color;
  return node;
}

LayoutNode divider_node() {
  LayoutNode node;
  node.type = NodeType::Divider;
  node.as_divider.color = Color::Blue;
  node.as_divider.thickness = 1;
  return node;
}

void append_line(LayoutNode& root, const char* label, const char* value) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%s %s", label, value);
  root.as_stack.children.push_back(text_node(buf));
}

}  // namespace

LayoutNode build_diagnostics_page(const DiagnosticsSnapshot& snap) {
  LayoutNode root;
  root.type = NodeType::VStack;
  root.as_stack.pad = 14;
  root.as_stack.gap = 7;

  root.as_stack.children.push_back(
      text_node("DIAGNOSTICS", TextSize::H2, Weight::Bold, Color::Blue));
  root.as_stack.children.push_back(divider_node());

  char buf[96];
  snprintf(buf, sizeof(buf), "%lu KB  PSRAM %lu KB",
           (unsigned long)(snap.free_heap / 1024),
           (unsigned long)(snap.free_psram / 1024));
  append_line(root, "HEAP", buf);

  if (snap.battery_ok) {
    snprintf(buf, sizeof(buf), "%u%%  %umV",
             (unsigned)snap.battery_pct, (unsigned)snap.battery_mv);
  } else {
    snprintf(buf, sizeof(buf), "--  %umV", (unsigned)snap.battery_mv);
  }
  append_line(root, "BATT", buf);

  if (snap.wifi_connected) {
    snprintf(buf, sizeof(buf), "%ld dBm", (long)snap.rssi_dbm);
  } else {
    snprintf(buf, sizeof(buf), "offline");
  }
  append_line(root, "RSSI", buf);

  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)snap.last_fetch_s);
  append_line(root, "LAST", buf);

  snprintf(buf, sizeof(buf), "0x%08lx  unchanged %u",
           (unsigned long)snap.data_fingerprint,
           (unsigned)snap.unchanged_fetches);
  append_line(root, "DATA", buf);

  snprintf(buf, sizeof(buf), "%lu", (unsigned long)snap.wake_count);
  append_line(root, "WAKE", buf);

  append_line(root, "RESULT", snap.last_result && snap.last_result[0] ? snap.last_result : "-");

  return root;
}

}  // namespace rcpaper
