#include "render/palette.h"
#include <cstring>

namespace rcpaper {

bool color_from_token(const char* token, Color* out) {
  if (!token || !*token || !out) return false;
  // Concrete E6 inks.
  if (std::strcmp(token, "black")  == 0) { *out = Color::Black;  return true; }
  if (std::strcmp(token, "white")  == 0) { *out = Color::White;  return true; }
  if (std::strcmp(token, "red")    == 0) { *out = Color::Red;    return true; }
  if (std::strcmp(token, "yellow") == 0) { *out = Color::Yellow; return true; }
  if (std::strcmp(token, "green")  == 0) { *out = Color::Green;  return true; }
  if (std::strcmp(token, "blue")   == 0) { *out = Color::Blue;   return true; }
  // Semantic tokens (DSL v1) map to saturated AMOLED colors.
  if (std::strcmp(token, "accent")  == 0) { *out = Color::Red;    return true; }
  if (std::strcmp(token, "warning") == 0) { *out = Color::Yellow; return true; }
  if (std::strcmp(token, "success") == 0) { *out = Color::Green;  return true; }
  if (std::strcmp(token, "info")    == 0) { *out = Color::Blue;   return true; }
  if (std::strcmp(token, "muted")   == 0) { *out = Color::Black;  return true; }
  return false;
}

const char* color_to_token(Color c) {
  switch (c) {
    case Color::Black:  return "black";
    case Color::White:  return "white";
    case Color::Red:    return "red";
    case Color::Yellow: return "yellow";
    case Color::Green:  return "green";
    case Color::Blue:   return "blue";
  }
  return "black";
}

// The X4 panel is 1-bit B&W: every ink except white renders as black. Collapsing
// here (rather than in each primitive) means the whole compose buffer only ever
// holds pure black or pure white, so the 1-bit pack in page.cpp is unambiguous.
uint32_t color_to_rgb(Color c) {
  return c == Color::White ? kRgbWhite : kRgbBlack;
}

uint16_t color_to_rgb565(Color c) {
  const uint32_t rgb = color_to_rgb(c);
  const uint8_t r = (rgb >> 16) & 0xFF;
  const uint8_t g = (rgb >> 8) & 0xFF;
  const uint8_t b = rgb & 0xFF;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

}  // namespace rcpaper
